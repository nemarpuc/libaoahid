// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Node open and close: a host policy failure before request 54, close timeouts and a
 * frozen lost link, a report in flight at close, and an unregister that fails. */
#include "sim/transport_support.hpp"

namespace transport_sim {

void test_node_open_host_policy_failure_precedes_request54() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* first_node = nullptr;
    open_public_keyboard_session(&context, &device, &first_node);

    aoahid_keyboard_options keyboard{};
    keyboard.struct_size = sizeof(keyboard);
    keyboard.usage_minimum = 0x04U;
    keyboard.usage_maximum = 0x65U;
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&keyboard, &spec) == AOAHID_OK);
    const std::size_t registrations_before = controls_for(54U).size();
    const aoahid_node_options reserved_node{sizeof(aoahid_node_options), 0U, 1U, 1U};
    aoahid_node* rejected = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, spec, &reserved_node, kControlTimeoutMs, &rejected) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(rejected == nullptr);
    AOAHID_CHECK(aoahid_last_error()->field != nullptr);
    if (aoahid_last_error()->field != nullptr) {
        AOAHID_CHECK(std::string_view(aoahid_last_error()->field) == "node.reserved_slots");
    }
    AOAHID_CHECK(controls_for(54U).size() == registrations_before);

    aoahid_spec_release(spec);
    AOAHID_CHECK(aoahid_node_close(first_node, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_node_close_timeout_distinction_and_loss_freeze() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node, 100U);
    const std::array<std::uint8_t, 2U> report{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_TIMED_OUT, 0, 0U);
    AOAHID_CHECK(aoahid_raw_submit(node, report.data(), report.size(), kSendTimeoutMs) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(aoahid_last_error()->transport_status == LIBUSB_TRANSFER_TIMED_OUT);
    // The failed close still consumed the Node.
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);

    aoahid_fake_libusb_reset();
    open_public_keyboard_session(&context, &device, &node);
    AOAHID_CHECK(aoahid_keyboard_key(node, 0x04U, 1U) == AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_NO_DEVICE, 0, 0U);
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_keyboard_key(node, 0x04U, 0U) == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_last_error()->transport_status == LIBUSB_TRANSFER_NO_DEVICE);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 57);
    AOAHID_CHECK(aoahid_device_latched_error(device) == AOAHID_ERR_NO_DEVICE);
    // The transport latch is independent of the Node's terminal completion.
    // Device teardown must still publish that callback once while consuming
    // the graph.
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_last_error()->transport_status == LIBUSB_TRANSFER_NO_DEVICE);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 57);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_node_close_retains_terminal_link_loss() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);
    const std::uint16_t hid_id = aoahid_node_id(node);
    const std::array<std::uint8_t, 2U> report{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_NO_DEVICE, 0, 0U);
    AOAHID_CHECK(aoahid_raw_submit(node, report.data(), report.size(), kSendTimeoutMs) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_OK);

    // Link loss makes present()==false, but it must not bypass the already
    // published terminal completion when the standalone Node remains owned by
    // the caller.
    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_last_error()->transport_status == LIBUSB_TRANSFER_NO_DEVICE);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 57);
    AOAHID_CHECK(aoahid_last_error()->node_id == hid_id);
    AOAHID_CHECK(aoahid_last_error()->report_id == 1U);
    AOAHID_CHECK(aoahid_last_error()->length == report.size());
    // The failed close still consumed the Node.
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_node_close_with_report_in_flight_keeps_node() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);
    submit_delayed_public_report(node);
    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, 1U) == AOAHID_CLOSE_PENDING);
    AOAHID_CHECK(aoahid_node_id(node) != 0U);
    aoahid_fake_libusb_make_pending_transfers_ready();
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, session_drain_timeout_ms) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_node_close_failed_unregister_consumes_node() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);
    const std::uint16_t hid_id = aoahid_node_id(node);
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_ERR_STALL);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 55);
    AOAHID_CHECK(aoahid_last_error()->node_id == hid_id);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    AOAHID_CHECK(controls_for(55U).size() == 1U);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

} // namespace transport_sim
