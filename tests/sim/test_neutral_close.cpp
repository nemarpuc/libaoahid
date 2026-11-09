// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Closing a Node sends its neutral report: a button-only touchpad, a controller whose
 * D-pad is centred, and a touch frame drained before the neutral report. */
#include "sim/transport_support.hpp"

namespace transport_sim {

void test_public_touchpad_button_only_close() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(12U, {5U, 2U});

    aoahid_context* context = nullptr;
    aoahid_context_options context_options = public_context_options();
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);

    const aoahid_device_info selected{candidate.bus,
                                      candidate.address,
                                      candidate.port_path.data(),
                                      candidate.port_path.size(),
                                      candidate.vendor_id,
                                      candidate.product_id,
                                      0U,
                                      candidate.serial.c_str(),
                                      candidate.product.c_str()};
    aoahid_device* device = nullptr;
    aoahid_device_options device_options = public_device_options();
    const std::uint32_t drain_timeout_ms = 100U;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);

    aoahid_touchpad_options touchpad{};
    touchpad.struct_size = sizeof(touchpad);
    touchpad.maximum_contacts = 2U;
    touchpad.contacts_per_report = 2U;
    touchpad.contact_id = {0, 15, 4U, {}};
    touchpad.x = {0, 1000, 16U, {}};
    touchpad.y = {0, 1000, 16U, {}};
    touchpad.contact_count = {0, 2, 2U, {}};
    touchpad.enable_scan_time = 1U;
    touchpad.scan_time = {0, 65535, 16U, {}};
    touchpad.scan_time_unit_100us = 1U;
    touchpad.enable_multi_packet_frames = 0U;
    touchpad.button_count = 1U;

    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_touchpad(&touchpad, &spec) == AOAHID_OK);
    const aoa::hid::FieldLayout* count =
        profile_field(spec, aoa::hid::FieldSemantic::contact_count);
    const aoa::hid::FieldLayout* button = profile_field(spec, aoa::hid::FieldSemantic::buttons);
    AOAHID_CHECK(count != nullptr && button != nullptr);

    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    aoahid_node* node = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, spec, &node_options, kControlTimeoutMs, &node) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_touchpad_button(node, 1U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);

    const auto payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 2U);
    if (payloads.size() == 2U && count != nullptr && button != nullptr) {
        AOAHID_CHECK(extract_report_value(payloads[0], *count) == 0U);
        AOAHID_CHECK(extract_report_value(payloads[0], *button) == 1U);
        AOAHID_CHECK(extract_report_value(payloads[1], *count) == 0U);
        AOAHID_CHECK(extract_report_value(payloads[1], *button) == 0U);
    }

    aoahid_spec_release(spec);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_public_controller_close_neutralizes_dpad() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(14U, {5U, 4U});

    aoahid_context* context = nullptr;
    aoahid_context_options context_options = public_context_options();
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);
    const aoahid_device_info selected{candidate.bus,
                                      candidate.address,
                                      candidate.port_path.data(),
                                      candidate.port_path.size(),
                                      candidate.vendor_id,
                                      candidate.product_id,
                                      0U,
                                      candidate.serial.c_str(),
                                      candidate.product.c_str()};
    aoahid_device* device = nullptr;
    aoahid_device_options device_options = public_device_options();
    const std::uint32_t drain_timeout_ms = 100U;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);

    const std::array<aoahid_gamepad_axis_option, 2U> axes{
        aoahid_gamepad_axis_option{
            AOAHID_AXIS_X, 0x01U, 0x30U, {-127, 127, 8U, {}}, 0, "ABS_X", "AXIS_X"},
        aoahid_gamepad_axis_option{
            AOAHID_AXIS_Y, 0x01U, 0x31U, {-127, 127, 8U, {}}, 0, "ABS_Y", "AXIS_Y"}};
    aoahid_gamepad_options controller{};
    controller.struct_size = sizeof(controller);
    controller.axes = axes.data();
    controller.axis_count = axes.size();
    controller.button_count = 5U;
    controller.button_usage_minimum = 1U;

    controller.dpad_representation = AOAHID_DPAD_BUTTONS;
    aoahid_spec* raw_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_gamepad(&controller, &raw_spec) == AOAHID_OK);
    const std::array<const aoa::hid::FieldLayout*, 4U> raw_dpad{
        profile_field(raw_spec, aoa::hid::FieldSemantic::buttons, 5U),
        profile_field(raw_spec, aoa::hid::FieldSemantic::buttons, 6U),
        profile_field(raw_spec, aoa::hid::FieldSemantic::buttons, 7U),
        profile_field(raw_spec, aoa::hid::FieldSemantic::buttons, 8U)};
    AOAHID_CHECK(std::all_of(raw_dpad.begin(), raw_dpad.end(),
                             [](const auto* field) { return field != nullptr; }));
    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    aoahid_node* raw_node = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, raw_spec, &node_options, kControlTimeoutMs, &raw_node) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_gamepad_dpad(raw_node, 1U, 0U, 0U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(raw_node, 100U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(raw_node, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    auto payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 2U);
    if (payloads.size() == 2U && std::all_of(raw_dpad.begin(), raw_dpad.end(),
                                             [](const auto* field) { return field != nullptr; })) {
        AOAHID_CHECK(extract_report_value(payloads[0], *raw_dpad[0]) == 1U);
        for (const auto* field : raw_dpad) {
            AOAHID_CHECK(extract_report_value(payloads[1], *field) == 0U);
        }
    }
    aoahid_spec_release(raw_spec);

    // The shared neutralization also clears the stored encoded value, but a
    // canonical Hat must continue to serialize its declared Null value.
    controller.dpad_representation = AOAHID_DPAD_HAT;
    controller.hat_logical_minimum = 0;
    controller.hat_logical_maximum = 7;
    controller.hat_bit_width = 4U;
    aoahid_spec* hat_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_gamepad(&controller, &hat_spec) == AOAHID_OK);
    const auto* hat = profile_field(hat_spec, aoa::hid::FieldSemantic::hat);
    AOAHID_CHECK(hat != nullptr);
    aoahid_node* hat_node = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, hat_spec, &node_options, kControlTimeoutMs, &hat_node) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_gamepad_dpad(hat_node, 1U, 0U, 0U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(hat_node, 100U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(hat_node, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 4U);
    if (payloads.size() == 4U && hat != nullptr) {
        AOAHID_CHECK(extract_report_value(payloads[2], *hat) == 0U);
        AOAHID_CHECK(extract_report_value(payloads[3], *hat) == 15U);
    }
    aoahid_spec_release(hat_spec);

    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_close_drains_touch_frame_before_neutral() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_touch_session(&context, &device, &node);

    // The submit's one nonblocking poll completes packet one, leaving a
    // nonzero packet cursor while no transfer is active.
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, -1, 0U);
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_pending_transfer_count() == 0U);
    const aoa::detail::TouchContact blocked_change{1U, 101, 201, 0, 0, 0, 0};
    AOAHID_CHECK(aoahid_touchscreen_contact(node, blocked_change.contact_id, 1U, blocked_change.x,
                                            blocked_change.y, nullptr) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(controls_for(57U).size() == 1U);

    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    // Initial frame: two packets. Neutral lift frame: two packets. Closing may
    // not splice the neutral state into the initial frame's second packet.
    AOAHID_CHECK(controls_for(57U).size() == 4U);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

} // namespace transport_sim
