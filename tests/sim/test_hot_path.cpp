// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* The caller-poll report path and the Channel path take no lock and allocate nothing
 * once a session is open. */
#include "sim/transport_support.hpp"

namespace transport_sim {

void test_caller_poll_hot_path_is_lock_and_allocation_free() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_keyboard_session(&context, &device, &node);
    AOAHID_CHECK(context->active_state_mutex == nullptr);
    AOAHID_CHECK(node->active_completion_mutex == nullptr);

    // Prewarm the fake event dispatcher's reusable callback array, then stop
    // recording request 57 payloads so only library allocations are measured.
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_OK);
    aoahid_fake_libusb_set_control_recording(0);
    const aoahid_fake_libusb_event_stats before = event_stats();
    allocation_probe::begin();
    const aoahid_result key_down = aoahid_keyboard_key(node, 0x04U, 1U);
    const aoahid_result first_submit = aoahid_node_submit(node, kSendTimeoutMs);
    const bool first_completed = !node->transfer_inflight.load(std::memory_order_acquire);
    const aoahid_result key_up = aoahid_keyboard_key(node, 0x04U, 0U);
    const aoahid_result second_submit = aoahid_node_submit(node, kSendTimeoutMs);
    const bool second_completed = !node->transfer_inflight.load(std::memory_order_acquire);
    const std::size_t hot_allocations = allocation_probe::finish();
    aoahid_fake_libusb_set_control_recording(1);

    AOAHID_CHECK(key_down == AOAHID_OK);
    AOAHID_CHECK(first_submit == AOAHID_OK);
    AOAHID_CHECK(first_completed);
    AOAHID_CHECK(key_up == AOAHID_OK);
    AOAHID_CHECK(second_submit == AOAHID_OK);
    AOAHID_CHECK(second_completed);
    const aoahid_fake_libusb_event_stats after = event_stats();
    // Each accepted report performs exactly one async submit followed by one
    // nonblocking event pass and one completion callback. Together with the
    // completed flags sampled before the next state change, these counts prove
    // there is no extra pre-submit event reap in the measured hot path.
    AOAHID_CHECK(after.successful_submits == before.successful_submits + 2U);
    AOAHID_CHECK(after.handle_calls == before.handle_calls + 2U);
    AOAHID_CHECK(after.zero_timeout_calls == before.zero_timeout_calls + 2U);
    AOAHID_CHECK(after.callbacks_dispatched == before.callbacks_dispatched + 2U);
    AOAHID_CHECK(after.waits_started == before.waits_started);
    AOAHID_CHECK(hot_allocations == 0U);

    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, session_drain_timeout_ms) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_channel_hot_path_is_lock_and_allocation_free() {
    aoahid_fake_libusb_reset();
    const Candidate phone = add_adb_phone();
    aoahid_context_options context_options = public_context_options();
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);
    const aoahid_device_info selected = public_info(phone);
    aoahid_device_options device_options = public_device_options();
    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);
    const aoahid_channel_options options = adb_channel_options();
    aoahid_channel* channel = nullptr;
    AOAHID_CHECK(aoahid_channel_open(device, &options, kDrainTimeoutMs, &channel) == AOAHID_OK);
    AOAHID_CHECK(context->active_state_mutex == nullptr);

    const std::array<std::uint8_t, 24> header{'O', 'K', 'A', 'Y'};
    aoahid_fake_libusb_push_bulk_in(0U, kAdbEndpointIn, header.data(), header.size());
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_OK);
    aoahid_fake_libusb_set_control_recording(0);
    std::array<std::uint8_t, 32> buffer{};
    std::size_t received = 0U;
    std::size_t written = 0U;
    allocation_probe::begin();
    const aoahid_result read =
        aoahid_channel_read(channel, buffer.data(), buffer.size(), &received, 0U);
    const aoahid_result write =
        aoahid_channel_write(channel, header.data(), header.size(), &written, 0U);
    const aoahid_result polled = aoahid_context_poll(context, 0U);
    const aoahid_result empty =
        aoahid_channel_read(channel, buffer.data(), buffer.size(), &received, 0U);
    const std::size_t hot_allocations = allocation_probe::finish();
    aoahid_fake_libusb_set_control_recording(1);

    AOAHID_CHECK(read == AOAHID_OK && written == header.size());
    AOAHID_CHECK(write == AOAHID_OK && polled == AOAHID_OK);
    AOAHID_CHECK(empty == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(hot_allocations == 0U);

    AOAHID_CHECK(aoahid_channel_close(channel, kDrainTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

} // namespace transport_sim
