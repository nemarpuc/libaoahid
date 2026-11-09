// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* What the transport sim tests share: the transport types, completion recorders,
 * fake-libusb record readers, and the public sessions the tests open. The tests
 * check the exact AOA control sequence and transfer lifecycle at the transport
 * boundary; the deterministic backend contains no HID policy. */
#pragma once

#include "api/internal.hpp"
#include "fake_libusb/libusb.h"
#include "sim/allocation_probe.hpp"
#include "test.hpp"
#include "transport/transport.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace transport_sim {

// Every wait is an argument of the call and has no default; these are the tests' own choices.
inline constexpr std::uint32_t kControlTimeoutMs = 20U;
inline constexpr std::uint32_t kSendTimeoutMs = 20U;
inline constexpr std::uint32_t kDrainTimeoutMs = 1U;
// In internal-thread mode the event thread delivers cancellations and callbacks
// asynchronously, so a close waits for it. This is only the upper bound of that
// wait: the call returns as soon as the transfers have drained.
inline constexpr std::uint32_t kThreadDrainTimeoutMs = 2000U;

using aoa::transport::Candidate;
using aoa::transport::Device;
using aoa::transport::DeviceConfig;
using aoa::transport::Port;
using aoa::transport::PreparedTransfer;
using aoa::transport::Runtime;

struct CompletionState {
    std::size_t calls{};
    aoahid_result result{AOAHID_OK};
    std::int32_t native_status{};
};

struct BlockingCompletionState {
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    std::atomic<aoahid_result> result{AOAHID_OK};
};

inline void completed(void* user, const aoahid_result result,
                      const std::int32_t native_status) noexcept {
    auto* state = static_cast<CompletionState*>(user);
    if (state != nullptr) {
        ++state->calls;
        state->result = result;
        state->native_status = native_status;
    }
}

inline void blocking_completed(void* user, const aoahid_result result,
                               const std::int32_t native_status) noexcept {
    static_cast<void>(native_status);
    auto* state = static_cast<BlockingCompletionState*>(user);
    if (state == nullptr) {
        return;
    }
    state->result.store(result, std::memory_order_relaxed);
    state->entered.store(true, std::memory_order_release);
    state->entered.notify_all();
    state->release.wait(false, std::memory_order_acquire);
}

inline DeviceConfig current_mode_config() {
    DeviceConfig config{};
    config.event_mode = AOAHID_EVENT_CALLER_POLL;
    config.descriptor_fragment_bytes = 64U;
    config.pool_slots = 2U;
    config.maximum_report_bytes = 64U;
    config.claim_policy = AOAHID_INTERFACE_CLAIM_NONE;
    config.interface_number = -1;
    return config;
}

inline Candidate add_candidate(const std::uint8_t address, const std::array<std::uint8_t, 2U>& path,
                               const std::uint16_t vendor = 0x18D1U,
                               const std::uint16_t product = 0x2D00U) {
    const aoahid_fake_libusb_device_config fake{1U,        address,       path.data(), path.size(),
                                                vendor,    product,       2U,          "serial",
                                                "Android", LIBUSB_SUCCESS};
    AOAHID_CHECK(aoahid_fake_libusb_add_device(&fake) >= 0);
    Candidate candidate{};
    candidate.bus = fake.bus;
    candidate.address = fake.address;
    candidate.port_path.assign(path.begin(), path.end());
    candidate.vendor_id = fake.vendor_id;
    candidate.product_id = fake.product_id;
    candidate.serial = fake.serial;
    candidate.product = fake.product;
    return candidate;
}

inline std::vector<aoahid_fake_libusb_control_record> controls_for(const std::uint8_t request) {
    std::vector<aoahid_fake_libusb_control_record> records;
    const std::size_t count = aoahid_fake_libusb_control_count();
    for (std::size_t index = 0U; index < count; ++index) {
        aoahid_fake_libusb_control_record record{};
        AOAHID_CHECK(aoahid_fake_libusb_get_control(index, &record) == LIBUSB_SUCCESS);
        if (record.request == request) {
            records.push_back(record);
        }
    }
    return records;
}

inline aoahid_fake_libusb_event_stats event_stats() {
    aoahid_fake_libusb_event_stats stats{};
    AOAHID_CHECK(aoahid_fake_libusb_get_event_stats(&stats) == LIBUSB_SUCCESS);
    return stats;
}

inline std::vector<std::vector<std::uint8_t>> control_payloads_for(const std::uint8_t request) {
    std::vector<std::vector<std::uint8_t>> payloads;
    const std::size_t count = aoahid_fake_libusb_control_count();
    for (std::size_t index = 0U; index < count; ++index) {
        aoahid_fake_libusb_control_record record{};
        AOAHID_CHECK(aoahid_fake_libusb_get_control(index, &record) == LIBUSB_SUCCESS);
        if (record.request != request)
            continue;
        std::vector<std::uint8_t> payload(record.length);
        AOAHID_CHECK(aoahid_fake_libusb_copy_control_data(index, payload.data(), payload.size()) ==
                     payload.size());
        payloads.push_back(std::move(payload));
    }
    return payloads;
}

inline std::uint64_t extract_report_value(const std::vector<std::uint8_t>& report,
                                          const aoa::hid::FieldLayout& field) {
    std::uint64_t value = 0U;
    for (std::uint8_t bit = 0U; bit < field.bit_width; ++bit) {
        const std::size_t position = field.bit_offset + bit;
        const std::uint64_t byte = report[position / 8U];
        value |= ((byte >> (position & 7U)) & std::uint64_t{1}) << bit;
    }
    return value;
}

inline const aoa::hid::FieldLayout* profile_field(const aoahid_spec* spec,
                                                  const aoa::hid::FieldSemantic semantic,
                                                  const std::uint16_t instance = 0U) {
    for (const auto& candidate : spec->layout.fields) {
        if (candidate.semantic == semantic && candidate.instance == instance)
            return &candidate;
    }
    return nullptr;
}

inline void poll_until(Runtime* runtime, CompletionState* state) {
    for (std::size_t attempt = 0U; attempt < 20U && state->calls == 0U; ++attempt) {
        AOAHID_CHECK(runtime->poll(1U) == AOAHID_OK);
    }
    AOAHID_CHECK(state->calls == 1U);
}

inline aoahid_context_options public_context_options();

inline aoahid_device_options public_device_options();

inline aoahid_device_info public_info(const Candidate& candidate) {
    return aoahid_device_info{candidate.bus,
                              candidate.address,
                              candidate.port_path.data(),
                              candidate.port_path.size(),
                              candidate.vendor_id,
                              candidate.product_id,
                              0U,
                              candidate.serial.c_str(),
                              candidate.product.c_str()};
}

inline constexpr std::uint32_t kAccessoryControlTimeoutMs = 500U;

inline aoahid_accessory_options accessory_options(const char* manufacturer, const char* model) {
    aoahid_accessory_options options{};
    options.struct_size = static_cast<std::uint32_t>(sizeof(options));
    options.strings.manufacturer = manufacturer;
    options.strings.model = model;
    return options;
}

inline std::vector<std::uint8_t> nul_terminated(const char* text) {
    return {reinterpret_cast<const std::uint8_t*>(text),
            reinterpret_cast<const std::uint8_t*>(text) + std::char_traits<char>::length(text) +
                1U};
}

inline constexpr std::uint8_t kAdbEndpointIn = 0x81U;

inline constexpr std::uint8_t kAdbEndpointOut = 0x01U;

inline constexpr std::uint16_t kAdbMaxPacket = 512U;

// An Android device exposing the ADB interface (AOSP adb.h: 0xFF/0x42/0x01).
inline Candidate add_adb_phone() {
    const Candidate phone = add_candidate(4U, {2U, 3U}, 0x18D1U, 0x4EE7U);
    const aoahid_fake_libusb_bulk_interface adb{
        1U, 0xFFU, 0x42U, 0x01U, kAdbEndpointIn, kAdbEndpointOut, kAdbMaxPacket};
    aoahid_fake_libusb_add_bulk_interface(0U, &adb);
    return phone;
}

// The ADB interface read as a stream of one-packet transfers, so a test sees each packet.
inline aoahid_channel_options adb_channel_options() {
    aoahid_channel_options options;
    aoahid_channel_options_init(&options);
    options.read_mode = AOAHID_CHANNEL_READ_STREAM;
    options.in_transfers = 2U;
    options.transfer_bytes = 512U;
    return options;
}

inline bool wait_until_true(const std::function<bool()>& ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!ready()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

inline aoahid_context_options public_context_options() {
    aoahid_context_options options{};
    options.struct_size = sizeof(options);
    options.event_mode = AOAHID_EVENT_CALLER_POLL;
    options.log_level = AOAHID_LOG_DISABLED;
    return options;
}

inline aoahid_device_options public_device_options() {
    aoahid_device_options options{};
    options.struct_size = sizeof(options);
    options.descriptor_fragment_bytes = 64U;
    options.transfer_pool_slots = 1U;
    options.maximum_report_bytes = 64U;
    options.validate_reports = 0U;
    options.aoa_descriptor_wire_policy_bytes = 1024U;
    options.linux_hid.descriptor_bytes = 1024U;
    options.linux_hid.fields_per_report = 256U;
    options.linux_hid.global_stack_depth = 4U;
    options.linux_hid.usages = 12288U;
    options.linux_hid.report_data_bits = 65528U;
    options.linux_hid.report_size_bits = 256U;
    options.target_ep0_data_policy_bytes = 4096U;
    options.host_control_buffer_policy_bytes = 4096U;
    options.interface_claim_policy = AOAHID_INTERFACE_CLAIM_NONE;
    options.interface_number = -1;
    return options;
}

// The drain wait the session's tests give when they close what a helper below opened.
inline std::uint32_t session_drain_timeout_ms = kDrainTimeoutMs;

inline void open_public_raw_session(aoahid_context** context, aoahid_device** device,
                                    aoahid_node** node,
                                    const std::uint32_t close_drain_timeout_ms = 1U) {
    static constexpr std::array<std::uint8_t, 2U> path{5U, 1U};
    const aoahid_fake_libusb_device_config fake{
        2U,      7U, path.data(),       path.size(), 0x18D1U,
        0x2D00U, 2U, "graveyard-phone", "Android",   LIBUSB_SUCCESS};
    AOAHID_CHECK(aoahid_fake_libusb_add_device(&fake) >= 0);

    aoahid_context_options context_options = public_context_options();
    AOAHID_CHECK(aoahid_context_create(&context_options, context) == AOAHID_OK);
    const aoahid_device_info selected{fake.bus,    fake.address,   path.data(),
                                      path.size(), fake.vendor_id, fake.product_id,
                                      0U,          fake.serial,    fake.product};
    aoahid_device_options device_options = public_device_options();
    session_drain_timeout_ms = close_drain_timeout_ms;
    AOAHID_CHECK(aoahid_device_open(*context, &selected, &device_options, device) == AOAHID_OK);

    static constexpr std::array<std::uint8_t, 21U> descriptor{
        0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x85U, 0x01U, 0x15U, 0x00U, 0x25U,
        0x01U, 0x75U, 0x01U, 0x95U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U};
    const aoahid_raw_report report{{1U, 1U, {}}, 2U};
    const aoahid_raw_options raw_options{sizeof(aoahid_raw_options),
                                         0U,
                                         descriptor.data(),
                                         descriptor.size(),
                                         &report,
                                         1U,
                                         1U,
                                         0U,
                                         0U};
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_raw(&raw_options, &spec) == AOAHID_OK);
    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    AOAHID_CHECK(aoahid_node_open(*device, spec, &node_options, kControlTimeoutMs, node) ==
                 AOAHID_OK);
    // The node's retained reference must keep the spec alive through a late
    // callback even after the caller drops its own reference.
    aoahid_spec_release(spec);
}

inline void open_public_keyboard_session(aoahid_context** context, aoahid_device** device,
                                         aoahid_node** node,
                                         const std::uint32_t close_timeout_ms = 100U) {
    static constexpr std::array<std::uint8_t, 2U> path{6U, 1U};
    const aoahid_fake_libusb_device_config fake{
        3U,      9U, path.data(),      path.size(), 0x18D1U,
        0x2D00U, 2U, "keyboard-phone", "Android",   LIBUSB_SUCCESS};
    AOAHID_CHECK(aoahid_fake_libusb_add_device(&fake) >= 0);
    aoahid_context_options context_config = public_context_options();
    AOAHID_CHECK(aoahid_context_create(&context_config, context) == AOAHID_OK);
    const aoahid_device_info selected{fake.bus,    fake.address,   path.data(),
                                      path.size(), fake.vendor_id, fake.product_id,
                                      0U,          fake.serial,    fake.product};
    aoahid_device_options device_config = public_device_options();
    session_drain_timeout_ms = close_timeout_ms;
    AOAHID_CHECK(aoahid_device_open(*context, &selected, &device_config, device) == AOAHID_OK);

    aoahid_keyboard_options keyboard{};
    keyboard.struct_size = sizeof(keyboard);
    keyboard.usage_minimum = 0x04U;
    keyboard.usage_maximum = 0x65U;
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&keyboard, &spec) == AOAHID_OK);
    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    AOAHID_CHECK(aoahid_node_open(*device, spec, &node_options, kControlTimeoutMs, node) ==
                 AOAHID_OK);
    aoahid_spec_release(spec);
}

inline void submit_delayed_public_report(aoahid_node* node);

inline void submit_delayed_public_report(aoahid_node* node) {
    static constexpr std::array<std::uint8_t, 2U> report{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, 2, 1'000U);
    AOAHID_CHECK(aoahid_raw_submit(node, report.data(), report.size(), kSendTimeoutMs) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_pending_transfer_count() == 1U);
}

inline void open_public_touch_session(aoahid_context** context, aoahid_device** device,
                                      aoahid_node** node, const bool enable_scan_time = false,
                                      const bool enable_battery = false) {
    static constexpr std::array<std::uint8_t, 2U> path{5U, 2U};
    const aoahid_fake_libusb_device_config fake{
        2U,      8U, path.data(),         path.size(), 0x18D1U,
        0x2D00U, 2U, "touch-close-phone", "Android",   LIBUSB_SUCCESS};
    AOAHID_CHECK(aoahid_fake_libusb_add_device(&fake) >= 0);

    aoahid_context_options context_options = public_context_options();
    AOAHID_CHECK(aoahid_context_create(&context_options, context) == AOAHID_OK);
    const aoahid_device_info selected{fake.bus,    fake.address,   path.data(),
                                      path.size(), fake.vendor_id, fake.product_id,
                                      0U,          fake.serial,    fake.product};
    aoahid_device_options device_options = public_device_options();
    // A short drain timeout is fine on a lightly loaded CI runner, but the
    // fake backend's async completion still crosses a real OS thread wakeup,
    // and some Windows runners (particularly ARM64) have shown enough
    // scheduling jitter to miss a 50 ms budget. Draining still returns as
    // soon as the pending frame completes, so a larger ceiling only adds
    // margin; it does not slow down the success path.
    session_drain_timeout_ms = 2000U;
    AOAHID_CHECK(aoahid_device_open(*context, &selected, &device_options, device) == AOAHID_OK);

    aoahid_touchscreen_options touch{};
    touch.struct_size = sizeof(touch);
    touch.maximum_contacts = 2U;
    touch.contacts_per_report = 1U;
    touch.contact_id = {0, 15, 4U, {}};
    touch.x = {0, 1000, 16U, {}};
    touch.y = {0, 1000, 16U, {}};
    touch.contact_count = {0, 2, 2U, {}};
    if (enable_scan_time) {
        touch.enable_scan_time = 1U;
        touch.scan_time = {0, 65535, 16U, {}};
        touch.scan_time_unit_100us = 1U;
    }
    touch.enable_multi_packet_frames = 1U;
    if (enable_battery) {
        touch.report_id = {1U, 1U, {0U, 0U, 0U}};
        touch.enable_battery = 1U;
        touch.battery.report_id = {1U, 2U, {0U, 0U, 0U}};
        touch.battery.strength = {1, 100, 8U, {}};
    }
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&touch, &spec) == AOAHID_OK);
    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    AOAHID_CHECK(aoahid_node_open(*device, spec, &node_options, kControlTimeoutMs, node) ==
                 AOAHID_OK);
    aoahid_spec_release(spec);

    const aoa::detail::TouchContact first{1U, 100, 200, 0, 0, 0, 0};
    const aoa::detail::TouchContact second{2U, 300, 400, 0, 0, 0, 0};
    AOAHID_CHECK(aoahid_touchscreen_contact(*node, first.contact_id, 1U, first.x, first.y,
                                            nullptr) == AOAHID_OK);
    AOAHID_CHECK(aoahid_touchscreen_contact(*node, second.contact_id, 1U, second.x, second.y,
                                            nullptr) == AOAHID_OK);
}

// The tests, in the order test_transport() runs them.
void test_runtime_initialization_options();
void test_registration_reservations_and_async_results();
void test_uncertain_register_attempts_unregister_and_preserves_error();
void test_descriptor_failure_unregisters_and_preserves_error();
void test_error_report_id_is_published_before_user_callback_returns();
void test_current_mode_open_and_explicit_claim();
void test_port_ownership_and_discovery_reuse();
void test_accessory_start_sends_51_52_53_and_closes();
void test_accessory_start_rejects_and_reports_failures();
void test_channel_shares_the_device_handle_internal_thread();
void test_channel_configures_an_unconfigured_device();
void test_channel_request_reads();
void test_hid_adb_and_accessory_bulk_run_together();
void test_channel_caller_poll_and_device_close();
void test_open_sends_no_aoa_request();
void test_required_device_policies_precede_usb_io();
void test_tuning_values_required_and_zero_reservation();
void test_initializers_open_a_working_session();
void test_cancel_and_device_isolation();
void test_blocking_event_wait_is_woken_by_cancel();
void test_public_touchpad_button_only_close();
void test_public_controller_close_neutralizes_dpad();
void test_descriptor_requirements_are_caller_policies();
void test_public_touch_scan_time_lifecycle();
void test_public_touch_scan_time_rejected_first_attempt_stays_zero();
void test_close_drains_touch_frame_before_neutral();
void test_battery_reports();
void test_battery_waits_for_the_touch_frame();
void test_battery_reports_internal_thread();
void test_public_error_contract_and_raw_padding();
void test_first_report_stall_is_returned_without_retry();
void test_submit_blocking_waits_for_a_free_slot_without_spinning();
void test_submit_rejection_retains_state_and_close_releases();
void test_node_open_host_policy_failure_precedes_request54();
void test_node_close_timeout_distinction_and_loss_freeze();
void test_node_close_retains_terminal_link_loss();
void test_node_close_with_report_in_flight_keeps_node();
void test_node_close_failed_unregister_consumes_node();
void test_caller_poll_hot_path_is_lock_and_allocation_free();
void test_channel_hot_path_is_lock_and_allocation_free();
void test_accepted_submit_defers_opportunistic_poll_error();
void test_context_destroy_aggregates_unregister_error();
void test_context_destroy_never_returns_terminal_pending();
void test_graveyard_publishes_unregister_and_cancel_errors();
void test_public_graveyard_delayed_cancel();
void test_public_graveyard_blocking_destroy();
void test_blocking_destroy_deadline_retains_context();

} // namespace transport_sim
