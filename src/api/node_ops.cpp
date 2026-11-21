// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * Node-side internals shared by the C ABI entry points: report state, neutralizing, and the
 * drain and teardown sequence of Nodes, Channels and Devices.
 */

#include "api/support.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <new>
#include <thread>
#include <utility>

using aoa::detail::close_live_node_until;
using aoa::detail::completion_reason;
using aoa::detail::consume_completion;
using aoa::detail::current_deferred_error;
using aoa::detail::DeferredErrorInput;
using aoa::detail::destroy_drained_device;
using aoa::detail::device_close_impl;
using aoa::detail::finish_transport_result;
using aoa::detail::graph_drained;
using aoa::detail::has_non_neutral_state;
using aoa::detail::initialize_node_state;
using aoa::detail::make_deferred_error;
using aoa::detail::park_in_graveyard;
using aoa::detail::poll_until_idle;
using aoa::detail::preflight_context;
using aoa::detail::publish_deferred_error;
using aoa::detail::release_channel_storage;
using aoa::detail::remaining_milliseconds;
using aoa::detail::report_id;
using aoa::detail::set_error;
using aoa::detail::wait_for_report_drain;

namespace {

bool touch_frame_in_progress(const aoahid_node* node) noexcept {
    const auto* touch =
        node == nullptr ? nullptr : std::get_if<aoa::detail::TouchState>(&node->state);
    return touch != nullptr && touch->packet_cursor != 0U;
}

} // namespace

namespace aoa::detail {

std::uint16_t report_id(const aoahid_node* node) noexcept {
    return node != nullptr && node->spec != nullptr && node->spec->layout.has_report_id
               ? node->spec->layout.report_id
               : 0U;
}

const char* completion_reason(const aoahid_result result) noexcept {
    switch (result) {
    case AOAHID_ERR_STALL:
        return "The device refused AOA request 57 (STALL); nothing was applied and the state "
               "stays pending for a resend. Right after aoahid_node_open this usually means "
               "Android has not finished registering the HID.";
    case AOAHID_ERR_TIMEOUT:
        return "AOA request 57 timed out; delivery to the accessory cannot be established.";
    case AOAHID_ERR_SHORT_TRANSFER:
        return "AOA request 57 completed with an actual payload length different from the "
               "requested length.";
    case AOAHID_ERR_NO_DEVICE:
        return "The device disappeared before or while AOA request 57 was sent.";
    case AOAHID_ERR_OVERFLOW:
        return "libusb completed AOA request 57 with a transfer-overflow status.";
    case AOAHID_ERR_IO:
        return "The asynchronous AOA request 57 transfer ended with a libusb I/O or cancellation "
               "status.";
    default:
        return "The asynchronous AOA request 57 transfer failed.";
    }
}

aoahid_result consume_completion(aoahid_node* node, const char* field) noexcept {
    const aoahid_result result =
        node->completion_result.exchange(AOAHID_OK, std::memory_order_acq_rel);
    if (result == AOAHID_OK) {
        node->completion_native_status.store(0, std::memory_order_relaxed);
        node->completion_report_length.store(0U, std::memory_order_relaxed);
        node->completion_report_id.store(0U, std::memory_order_relaxed);
        return AOAHID_OK;
    }
    const std::int32_t native_status =
        node->completion_native_status.exchange(0, std::memory_order_relaxed);
    const std::uint32_t length =
        node->completion_report_length.exchange(0U, std::memory_order_relaxed);
    const std::uint16_t completed_report_id =
        node->completion_report_id.exchange(0U, std::memory_order_relaxed);
    set_error(result, field, completion_reason(result), native_status, 57, node->hid_id,
              completed_report_id, 0U, length);
    aoa::detail::set_error_node(node->node_id);
    return result;
}

// drop_battery is the close path: a battery level has no neutral value, so an
// unsent one is discarded instead of being sent on the way out.
aoahid_result poll_until_idle(aoahid_node* node, const std::uint32_t timeout_ms,
                              const bool drop_battery) {
    if (timeout_ms == 0U) {
        set_error(AOAHID_ERR_UNSET_FIELD, "timeout_ms", "A nonzero timeout is required.");
        return AOAHID_ERR_UNSET_FIELD;
    }
    const aoahid_result preflight = preflight_context(node->device->context);
    if (preflight != AOAHID_OK)
        return preflight;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    do {
        // Set when every pool slot is taken by other Nodes: this Node has no
        // transfer of its own to wait for, so it waits for any slot instead.
        bool pool_busy = false;
        std::uint64_t slot_generation = 0U;
        if (!node->transfer_inflight.load(std::memory_order_acquire)) {
            const aoahid_result completion = consume_completion(node, "node.completion");
            if (completion != AOAHID_OK)
                return completion;
            if (drop_battery) {
                node->battery_dirty = false;
            }
            if (!node->dirty && !node->battery_dirty)
                return AOAHID_OK;
            slot_generation = node->device->slot_generation.load();
            // The blocking timeout also bounds each report it sends: what is left of it.
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  deadline - std::chrono::steady_clock::now())
                                  .count();
            const std::uint32_t send_timeout_ms = static_cast<std::uint32_t>(std::clamp<long long>(
                left, 1LL, static_cast<long long>(std::numeric_limits<std::uint32_t>::max())));
            const aoahid_result submit = aoahid_node_submit(node, send_timeout_ms);
            if (submit != AOAHID_OK && submit != AOAHID_ERR_BUSY)
                return submit;
            pool_busy = submit == AOAHID_ERR_BUSY &&
                        !node->transfer_inflight.load(std::memory_order_acquire);
        }
        if (node->device->context->options.event_mode == AOAHID_EVENT_CALLER_POLL) {
            const aoahid_result poll = node->device->context->runtime->poll(1U);
            if (poll != AOAHID_OK && poll != AOAHID_ERR_TIMEOUT) {
                return finish_transport_result(poll, "context.poll",
                                               "libusb event handling failed.", node);
            }
        } else if (pool_busy) {
            // Woken by the next completion on this Device (see
            // transfer_complete()); a bump before this wait makes it return
            // at once, so no release is missed. The quantum only rechecks
            // Context errors.
            aoahid_device* device = node->device;
            std::unique_lock<std::mutex> lock(device->slot_mutex);
            device->slot_waiters.fetch_add(1U);
            const auto quantum = std::chrono::steady_clock::now() + std::chrono::milliseconds(10U);
            static_cast<void>(device->slot_cv.wait_until(
                lock, std::min(deadline, quantum), [device, slot_generation]() noexcept {
                    return device->slot_generation.load() != slot_generation;
                }));
            device->slot_waiters.fetch_sub(1U);
            lock.unlock();
            const aoahid_result event_error = preflight_context(node->device->context);
            if (event_error != AOAHID_OK)
                return event_error;
        } else {
            std::unique_lock<std::mutex> lock(node->completion_mutex);
            node->completion_waiters.fetch_add(1U, std::memory_order_acq_rel);
            const auto quantum = std::chrono::steady_clock::now() + std::chrono::milliseconds(10U);
            static_cast<void>(node->completion_cv.wait_until(
                lock, std::min(deadline, quantum), [node]() noexcept {
                    return !node->transfer_inflight.load(std::memory_order_acquire);
                }));
            node->completion_waiters.fetch_sub(1U, std::memory_order_acq_rel);
            // Do not nest the Context mutex below the per-Node completion
            // mutex. The event thread publishes Context errors independently.
            lock.unlock();
            const aoahid_result event_error = preflight_context(node->device->context);
            if (event_error != AOAHID_OK)
                return event_error;
        }
    } while (std::chrono::steady_clock::now() < deadline);
    set_error(AOAHID_ERR_TIMEOUT, "node.submit_blocking",
              "The report sequence did not reach a terminal callback before the deadline.", 0, 57,
              node->hid_id, report_id(node), 0U,
              static_cast<std::uint32_t>(node->spec->layout.wire_bytes));
    aoa::detail::set_error_node(node->node_id);
    return AOAHID_ERR_TIMEOUT;
}

std::uint32_t
remaining_milliseconds(const std::chrono::steady_clock::time_point deadline) noexcept {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return 0U;
    }
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    if (remaining <= 0) {
        return 1U;
    }
    return remaining > static_cast<std::chrono::milliseconds::rep>(
                           std::numeric_limits<std::uint32_t>::max())
               ? std::numeric_limits<std::uint32_t>::max()
               : static_cast<std::uint32_t>(remaining);
}

aoahid_result drain_node_for_close(aoahid_node* node,
                                   const std::chrono::steady_clock::time_point deadline) noexcept {
    aoahid_device* device = node->device;
    aoa::detail::DeferredError failure{};
    // Every step records its own error detail, which is kept for the first failure.
    const auto keep_first = [&](const aoahid_result result) {
        if (result != AOAHID_OK && !failure.pending)
            failure = current_deferred_error(result, "node.close", "A close step failed.", node);
    };
    const auto still_in_flight = [&]() {
        if (!node->transfer_inflight.load(std::memory_order_acquire))
            return false;
        set_error(AOAHID_CLOSE_PENDING, "node.close",
                  "A report transfer of the Node was still in flight at the close deadline.", 0, 57,
                  node->hid_id, report_id(node), 0U,
                  static_cast<std::uint32_t>(node->spec->layout.wire_bytes));
        aoa::detail::set_error_node(node->node_id);
        return true;
    };

    // A touch frame that has started must be finished before the neutral frame replaces it.
    if (node->transfer_inflight.load(std::memory_order_acquire) || touch_frame_in_progress(node)) {
        const std::uint32_t remaining = remaining_milliseconds(deadline);
        if (remaining != 0U)
            keep_first(poll_until_idle(node, remaining, true));
    }
    if (still_in_flight())
        return AOAHID_CLOSE_PENDING;
    keep_first(consume_completion(node, "node.completion"));
    node->battery_dirty = false;

    // A Node the phone does not have (staged, or its registration unapplied) has nobody to tell.
    if (device->transport->present() && node->hid_id != 0U) {
        make_neutral(node);
        if (node->dirty) {
            const std::uint32_t remaining = remaining_milliseconds(deadline);
            if (remaining == 0U) {
                set_error(AOAHID_ERR_TIMEOUT, "node.close",
                          "No time was left to send the neutral report.");
                keep_first(AOAHID_ERR_TIMEOUT);
            } else {
                keep_first(poll_until_idle(node, remaining, true));
            }
            if (still_in_flight())
                return AOAHID_CLOSE_PENDING;
        }
    }
    return failure.pending ? publish_deferred_error(failure) : AOAHID_OK;
}

// The Node stays live only while a transfer of it is in flight, because the transfer's callback
// still points at it: that case returns AOAHID_CLOSE_PENDING. Every other result means the Node is
// gone. A step that fails does not stop the ones after it, and the first failure is returned.
aoahid_result close_live_node_until(aoahid_node* node,
                                    const std::chrono::steady_clock::time_point deadline,
                                    const aoahid_commit_timeouts& timeouts) {
    aoahid_device* device = node->device;
    aoa::detail::Registration* registration = node->registration;
    aoa::detail::DeferredError failure{};
    const auto keep_first = [&](const aoahid_result result) {
        if (result != AOAHID_OK && !failure.pending)
            failure = current_deferred_error(result, "node.close", "A close step failed.", node);
    };

    const aoahid_result drained = drain_node_for_close(node, deadline);
    if (drained == AOAHID_CLOSE_PENDING)
        return AOAHID_CLOSE_PENDING;
    keep_first(drained);
    if (device->transport->clear_reservation(node->reservation) != AOAHID_OK) {
        set_error(AOAHID_CLOSE_PENDING, "node.reserved_slots",
                  "The Node's reserved transfer slot is still in use.", 0, 57, node->hid_id);
        aoa::detail::set_error_node(node->node_id);
        return AOAHID_CLOSE_PENDING;
    }

    // The Node leaves its registration. The phone's HID device still has its application until
    // the registration is applied: now, or at the end of a batch.
    auto& nodes = device->nodes;
    nodes.erase(std::remove(nodes.begin(), nodes.end(), node), nodes.end());
    if (registration != nullptr) {
        auto& members = registration->members;
        members.erase(std::remove(members.begin(), members.end(), node), members.end());
        node->registration = nullptr;
        try {
            aoa::detail::mark_stale(device, registration);
        } catch (...) {
            // The registration keeps its applied state and is not queued: the next close, commit
            // or batch end applies it again. The error is the allocation failure.
            registration->stale = true;
            keep_first(AOAHID_ERR_INTERNAL);
        }
        if (!device->in_batch) {
            try {
                keep_first(aoa::detail::apply_registration(device, registration, timeouts));
            } catch (...) {
                // An allocation failed while composing: the Node is gone either way, and the
                // registration stays stale for the next commit.
                registration->stale = true;
                keep_first(AOAHID_ERR_INTERNAL);
            }
            aoa::detail::forget_empty_registrations(device);
        }
    }
    aoa::detail::log_event(device->context, AOAHID_LOG_INFO, "node_close", device, node,
                           aoa::detail::LogEventStatus{failure.transport.result, 0, 55});
    aoa::detail::release_node_storage(node);
    return publish_deferred_error(failure);
}

} // namespace aoa::detail
