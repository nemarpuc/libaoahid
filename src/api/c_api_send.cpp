// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * Public C ABI: sending reports.
 */

#include "api/support.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

using aoa::detail::consume_completion;
using aoa::detail::current_deferred_error;
using aoa::detail::finish_transport_result;
using aoa::detail::has_non_neutral_state;
using aoa::detail::latch_context_error;
using aoa::detail::poll_until_idle;
using aoa::detail::preflight_context;
using aoa::detail::report_id;
using aoa::detail::set_error;
using aoa::detail::timeout_is_set;

extern "C" {

aoahid_result AOAHID_CALL aoahid_node_submit(aoahid_node* node,
                                             const std::uint32_t send_timeout_ms) try {
    AOAHID_SERIALIZED(aoa::detail::serialization_context(node), "aoahid_node_submit");
    aoa::detail::clear_error();
    if (!timeout_is_set(send_timeout_ms, "send_timeout_ms")) {
        return AOAHID_ERR_UNSET_FIELD;
    }
    if (node == nullptr || node->closed || node->device == nullptr || node->spec == nullptr) {
        set_error(AOAHID_ERR_PARAM, "node.submit",
                  "A live Node attached to a Device and Spec is required.");
        return AOAHID_ERR_PARAM;
    }
    if (node->spec->kind == AOAHID_PROFILE_RAW) {
        set_error(AOAHID_ERR_UNSUPPORTED, "node.submit",
                  "Raw nodes require aoahid_raw_submit() with an explicit report buffer.");
        return AOAHID_ERR_UNSUPPORTED;
    }
    const aoahid_result deferred = preflight_context(node->device->context);
    if (deferred != AOAHID_OK)
        return deferred;
    if (node->transfer_inflight.exchange(true, std::memory_order_acq_rel)) {
        set_error(AOAHID_ERR_BUSY, "node.submit", "The node already has one report in flight.");
        return AOAHID_ERR_BUSY;
    }
    const aoahid_result completion = consume_completion(node, "node.completion");
    if (completion != AOAHID_OK) {
        node->transfer_inflight.store(false, std::memory_order_release);
        return completion;
    }
    // Input always goes first; the battery report is sent only by a submit
    // that has no input report to send.
    const bool battery_report = !node->dirty && node->battery_dirty;
    if (!node->dirty && !battery_report) {
        node->transfer_inflight.store(false, std::memory_order_release);
        aoa::detail::clear_error();
        return AOAHID_OK;
    }
    const aoa::hid::ReportLayout& layout =
        battery_report ? node->spec->battery_layout : node->spec->layout;
    aoa::transport::PreparedTransfer prepared{};
    const aoahid_result acquired =
        node->device->transport->acquire(node->hid_id, node->reservation, layout.wire_bytes, node,
                                         &aoa::detail::transfer_complete, &prepared);
    if (acquired != AOAHID_OK) {
        node->transfer_inflight.store(false, std::memory_order_release);
        return finish_transport_result(acquired, "node.submit",
                                       "No transfer slot could be acquired for AOA request 57.",
                                       node, 57, 0U, static_cast<std::uint32_t>(layout.wire_bytes));
    }
    auto* scan_touch =
        battery_report ? nullptr : std::get_if<aoa::detail::TouchState>(&node->state);
    const auto* scan_config = std::get_if<aoa::detail::TouchConfig>(&node->spec->config);
    const bool update_scan_time = scan_touch != nullptr && scan_touch->packet_cursor == 0U &&
                                  scan_config != nullptr &&
                                  scan_config->fields.enable_scan_time == 1U;
    const std::uint32_t previous_scan_time =
        update_scan_time ? scan_touch->scan_time : std::uint32_t{0};
    const auto previous_scan_epoch =
        update_scan_time ? scan_touch->scan_epoch : std::chrono::steady_clock::time_point{};
    const bool previous_scan_epoch_active = update_scan_time && scan_touch->scan_epoch_active;
    const auto rollback_scan_time = [&]() noexcept {
        if (update_scan_time) {
            scan_touch->scan_time = previous_scan_time;
            scan_touch->scan_epoch = previous_scan_epoch;
            scan_touch->scan_epoch_active = previous_scan_epoch_active;
        }
    };
    if (update_scan_time) {
        const auto now = std::chrono::steady_clock::now();
        if (!scan_touch->scan_epoch_active) {
            scan_touch->scan_epoch = now;
            scan_touch->scan_epoch_active = true;
            scan_touch->scan_time = 0U;
        } else {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::microseconds>(now - scan_touch->scan_epoch)
                    .count();
            const std::uint64_t ticks =
                elapsed <= 0 ? 0U : static_cast<std::uint64_t>(elapsed) / 100U;
            const std::uint64_t modulus =
                static_cast<std::uint64_t>(scan_config->fields.scan_time.logical_maximum) + 1U;
            scan_touch->scan_time = static_cast<std::uint32_t>(ticks % modulus);
        }
    }
    std::size_t report_length = 0U;
    const aoahid_result serialized =
        battery_report ? aoa::detail::serialize_battery(node, prepared.payload, prepared.capacity,
                                                        &report_length)
                       : aoa::detail::serialize_node(node, prepared.payload, prepared.capacity,
                                                     &report_length);
    if (serialized != AOAHID_OK) {
        node->device->transport->abandon(prepared);
        rollback_scan_time();
        node->transfer_inflight.store(false, std::memory_order_release);
        return serialized;
    }
    if (node->device->validate_reports) {
        const aoahid_result validated = aoa::detail::validate_serialized_report(
            node->spec, layout, prepared.payload, report_length);
        if (validated != AOAHID_OK) {
            node->device->transport->abandon(prepared);
            rollback_scan_time();
            node->transfer_inflight.store(false, std::memory_order_release);
            return validated;
        }
    }
    node->submitted_battery = battery_report;
    node->submitted_non_neutral = !battery_report && has_non_neutral_state(node);
    node->submitted_report_length = report_length;
    node->submitted_report_id = battery_report ? layout.report_id : report_id(node);
    const aoahid_result result = node->device->transport->submit(
        prepared, report_length, node->submitted_report_id, send_timeout_ms);
    if (result != AOAHID_OK) {
        // Submit-time failures invoke the completion path synchronously. The
        // return value already reports that failure, so do not report it twice.
        rollback_scan_time();
        const aoahid_result completion_failure = consume_completion(node, "node.submit");
        if (completion_failure == AOAHID_OK) {
            return finish_transport_result(
                result, "node.submit",
                "libusb rejected AOA request 57 before asynchronous completion.", node, 57, 0U,
                static_cast<std::uint32_t>(report_length));
        }
        return result;
    }
    if (result == AOAHID_OK &&
        node->device->context->options.event_mode == AOAHID_EVENT_CALLER_POLL &&
        node->transfer_inflight.load(std::memory_order_acquire)) {
        const aoahid_result poll_result = node->device->context->runtime->poll(0U);
        if (poll_result != AOAHID_OK) {
            latch_context_error(
                node->device->context,
                current_deferred_error(poll_result, "context.event_pump",
                                       "The opportunistic caller-poll event handler failed after "
                                       "accepting the report."));
        }
    }
    aoa::detail::clear_error();
    return result;
}
AOAHID_C_RESULT_CATCH("node.submit")

aoahid_result AOAHID_CALL aoahid_node_submit_blocking(aoahid_node* node,
                                                      const std::uint32_t timeout_ms) try {
    AOAHID_SERIALIZED(aoa::detail::serialization_context(node), "aoahid_node_submit_blocking");
    aoa::detail::clear_error();
    if (node == nullptr || node->closed || node->device == nullptr || node->spec == nullptr) {
        set_error(AOAHID_ERR_PARAM, "node.submit_blocking",
                  "A live Node with an attached Device and Spec is required.");
        return AOAHID_ERR_PARAM;
    }
    const aoahid_result result = poll_until_idle(node, timeout_ms);
    return aoa::detail::finish_result(result, "node.submit_blocking",
                                      "The blocking report sequence failed.");
}
AOAHID_C_RESULT_CATCH("node.submit_blocking")

aoahid_result AOAHID_CALL aoahid_raw_submit(aoahid_node* node, const std::uint8_t* report,
                                            const std::size_t length,
                                            const std::uint32_t send_timeout_ms) try {
    AOAHID_SERIALIZED(aoa::detail::serialization_context(node), "aoahid_raw_submit");
    aoa::detail::clear_error();
    if (!timeout_is_set(send_timeout_ms, "send_timeout_ms")) {
        return AOAHID_ERR_UNSET_FIELD;
    }
    if (node == nullptr || node->closed || node->device == nullptr ||
        node->device->transport == nullptr || node->spec == nullptr ||
        node->spec->kind != AOAHID_PROFILE_RAW || report == nullptr) {
        set_error(AOAHID_ERR_PARAM, "raw.submit", "A live idle raw node and report are required.");
        return AOAHID_ERR_PARAM;
    }
    const aoahid_result deferred = preflight_context(node->device->context);
    if (deferred != AOAHID_OK)
        return deferred;
    if (node->transfer_inflight.exchange(true, std::memory_order_acq_rel)) {
        set_error(AOAHID_ERR_BUSY, "raw.submit", "The raw node already has one report in flight.");
        return AOAHID_ERR_BUSY;
    }
    const aoahid_result completion = consume_completion(node, "raw.completion");
    if (completion != AOAHID_OK) {
        node->transfer_inflight.store(false, std::memory_order_release);
        return completion;
    }
    const auto* raw = std::get_if<aoa::detail::RawConfig>(&node->spec->config);
    const auto accepted =
        std::find_if(raw->reports.begin(), raw->reports.end(), [=](const auto& item) {
            return item.wire_length == length &&
                   ((item.report_id.enabled == 0U) ||
                    (length != 0U && report[0] == item.report_id.value));
        });
    if (accepted == raw->reports.end()) {
        node->transfer_inflight.store(false, std::memory_order_release);
        set_error(AOAHID_ERR_PARAM, "raw.report",
                  "The report ID or exact wire length is not accepted by the raw spec.");
        return AOAHID_ERR_PARAM;
    }
    const std::size_t accepted_index = static_cast<std::size_t>(accepted - raw->reports.begin());
    const std::uint8_t valid_mask = raw->trailing_valid_masks[accepted_index];
    if ((report[length - 1U] & static_cast<std::uint8_t>(~valid_mask)) != 0U) {
        node->transfer_inflight.store(false, std::memory_order_release);
        set_error(AOAHID_ERR_PARAM, "raw.report.trailing_padding",
                  "Unused trailing Input-report bits must be zero.", 0, 57, node->hid_id,
                  accepted->report_id.enabled == 1U ? accepted->report_id.value : 0U,
                  static_cast<std::uint32_t>(length - 1U), 1U);
        return AOAHID_ERR_PARAM;
    }
    aoa::transport::PreparedTransfer prepared{};
    aoahid_result result = node->device->transport->acquire(
        node->hid_id, node->reservation, length, node, &aoa::detail::transfer_complete, &prepared);
    if (result != AOAHID_OK) {
        node->transfer_inflight.store(false, std::memory_order_release);
        return finish_transport_result(result, "raw.submit",
                                       "No transfer slot could be acquired for raw AOA request 57.",
                                       node, 57, 0U,
                                       static_cast<std::uint32_t>(std::min<std::size_t>(
                                           length, std::numeric_limits<std::uint32_t>::max())));
    }
    std::memcpy(prepared.payload, report, length);
    node->dirty = true;
    node->submitted_non_neutral = false;
    node->submitted_report_length = length;
    node->submitted_report_id = accepted->report_id.enabled == 1U ? accepted->report_id.value : 0U;
    result = node->device->transport->submit(prepared, length, node->submitted_report_id,
                                             send_timeout_ms);
    if (result != AOAHID_OK) {
        const aoahid_result completion_failure = consume_completion(node, "raw.submit");
        if (completion_failure == AOAHID_OK) {
            return finish_transport_result(
                result, "raw.submit",
                "libusb rejected raw AOA request 57 before asynchronous completion.", node, 57, 0U,
                static_cast<std::uint32_t>(
                    std::min<std::size_t>(length, std::numeric_limits<std::uint32_t>::max())));
        }
        return result;
    }
    aoa::detail::clear_error();
    return result;
}
AOAHID_C_RESULT_CATCH("raw.submit")

} // extern "C"
