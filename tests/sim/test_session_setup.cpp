// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* A public session from the initializers: every tuning value is required, a zero
 * reservation is refused, and descriptor requirements are the caller's policies. */
#include "sim/transport_support.hpp"

namespace transport_sim {

void test_tuning_values_required_and_zero_reservation() {
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
    // No tuning value has a default: each zero is refused.
    aoahid_device* device = nullptr;
    for (std::uint32_t aoahid_device_options::*field :
         {&aoahid_device_options::descriptor_fragment_bytes,
          &aoahid_device_options::transfer_pool_slots,
          &aoahid_device_options::maximum_report_bytes}) {
        aoahid_device_options zero = public_device_options();
        zero.*field = 0U;
        AOAHID_CHECK(aoahid_device_open(context, &selected, &zero, &device) ==
                         AOAHID_ERR_UNSET_FIELD &&
                     device == nullptr);
    }

    // The values the test used to get from zero, now given.
    aoahid_device_options options = public_device_options();
    options.transfer_pool_slots = 8U;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) == AOAHID_OK);
    AOAHID_CHECK(device != nullptr);
    if (device != nullptr) {
        AOAHID_CHECK(device->config.pool_slots == options.transfer_pool_slots);
    }

    aoahid_keyboard_options keyboard{};
    keyboard.struct_size = sizeof(keyboard);
    keyboard.usage_minimum = 0x04U;
    keyboard.usage_maximum = 0x65U;
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&keyboard, &spec) == AOAHID_OK);
    aoahid_node_options node_options{};
    node_options.struct_size = sizeof(node_options);
    aoahid_node* node = nullptr;
    // A zero wait is refused before anything is sent, and nothing is consumed.
    AOAHID_CHECK(aoahid_node_open(device, spec, &node_options, 0U, &node) ==
                     AOAHID_ERR_UNSET_FIELD &&
                 node == nullptr);
    AOAHID_CHECK(aoahid_last_error()->field != nullptr &&
                 std::string_view(aoahid_last_error()->field) == "control_timeout_ms");
    AOAHID_CHECK(controls_for(54U).empty());
    // The waits are the call's own: distinct values show which one reaches libusb.
    constexpr std::uint32_t open_control_timeout_ms = 777U;
    constexpr std::uint32_t submit_timeout_ms = 1234U;
    // The wait this test used to give through the options.
    constexpr std::uint32_t drain_timeout_ms = 1000U;
    AOAHID_CHECK(aoahid_node_open(device, spec, &node_options, open_control_timeout_ms, &node) ==
                 AOAHID_OK);
    AOAHID_CHECK(node != nullptr && node->reserved_slots == 0U);
    for (const std::uint8_t request : {std::uint8_t{54U}, std::uint8_t{56U}}) {
        const auto records = controls_for(request);
        AOAHID_CHECK(!records.empty());
        for (const auto& record : records)
            AOAHID_CHECK(record.timeout == open_control_timeout_ms);
    }

    AOAHID_CHECK(aoahid_keyboard_key(node, 0x04U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit(node, 0U) == AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_node_submit(node, submit_timeout_ms) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_OK);
    const auto reports = controls_for(57U);
    AOAHID_CHECK(!reports.empty() && reports.front().timeout == submit_timeout_ms);

    AOAHID_CHECK(aoahid_node_close(node, 0U, drain_timeout_ms) == AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, 0U) == AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    aoahid_spec_release(spec);
    AOAHID_CHECK(aoahid_device_close(device, 0U, drain_timeout_ms) == AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, 0U) == AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

// Only the initializers' values: a Context, a Device and a Node of every profile that has a
// complete initializer, and one report through it.
void test_initializers_open_a_working_session() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(13U, {5U, 3U});
    aoahid_context_options context_options;
    aoahid_context_options_init(&context_options);
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);
    const aoahid_device_info selected = public_info(candidate);
    aoahid_device_options device_options;
    aoahid_device_options_init(&device_options);
    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);

    aoahid_keyboard_options keyboard;
    aoahid_keyboard_options_init(&keyboard);
    aoahid_mouse_options mouse;
    aoahid_mouse_options_init(&mouse);
    aoahid_gamepad_options gamepad;
    aoahid_gamepad_options_init(&gamepad);
    aoahid_touchscreen_options touchscreen;
    aoahid_touchscreen_options_init(&touchscreen);
    aoahid_touchpad_options touchpad;
    aoahid_touchpad_options_init(&touchpad);
    aoahid_pen_options pen;
    aoahid_pen_options_init(&pen);
    aoahid_digitizer_options digitizer;
    aoahid_digitizer_options_init(&digitizer);
    std::array<aoahid_spec*, 7U> specs{};
    AOAHID_CHECK(aoahid_spec_create_keyboard(&keyboard, &specs[0]) == AOAHID_OK);
    AOAHID_CHECK(aoahid_spec_create_mouse(&mouse, &specs[1]) == AOAHID_OK);
    AOAHID_CHECK(aoahid_spec_create_gamepad(&gamepad, &specs[2]) == AOAHID_OK);
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&touchscreen, &specs[3]) == AOAHID_OK);
    AOAHID_CHECK(aoahid_spec_create_touchpad(&touchpad, &specs[4]) == AOAHID_OK);
    AOAHID_CHECK(aoahid_spec_create_pen(&pen, &specs[5]) == AOAHID_OK);
    AOAHID_CHECK(aoahid_spec_create_digitizer(&digitizer, &specs[6]) == AOAHID_OK);

    aoahid_node_options node_options;
    aoahid_node_options_init(&node_options);
    std::array<aoahid_node*, 7U> nodes{};
    for (std::size_t index = 0U; index < specs.size(); ++index) {
        AOAHID_CHECK(aoahid_node_open(device, specs[index], &node_options,
                                      AOAHID_CONTROL_TIMEOUT_MS, &nodes[index]) == AOAHID_OK);
        aoahid_spec_release(specs[index]);
    }
    AOAHID_CHECK(aoahid_keyboard_key(nodes[0], 0x04U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(nodes[0], AOAHID_SEND_TIMEOUT_MS) == AOAHID_OK);
    AOAHID_CHECK(aoahid_keyboard_key(nodes[0], 0x04U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(nodes[0], AOAHID_SEND_TIMEOUT_MS) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy_blocking(context, AOAHID_CONTROL_TIMEOUT_MS,
                                                 AOAHID_DRAIN_TIMEOUT_MS,
                                                 AOAHID_DRAIN_TIMEOUT_MS) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_descriptor_requirements_are_caller_policies() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(13U, {5U, 3U});
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

    aoahid_keyboard_options keyboard{};
    keyboard.struct_size = sizeof(keyboard);
    keyboard.usage_minimum = 0x04U;
    keyboard.usage_maximum = 0x65U;
    aoahid_spec* keyboard_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&keyboard, &keyboard_spec) == AOAHID_OK);
    AOAHID_CHECK(keyboard_spec != nullptr);
    if (keyboard_spec != nullptr) {
        AOAHID_CHECK(keyboard_spec->requirements.maximum_fields_per_report == 2U);
        AOAHID_CHECK(keyboard_spec->requirements.maximum_global_stack_depth == 0U);
        AOAHID_CHECK(keyboard_spec->requirements.maximum_report_size_bits == 6U);
        AOAHID_CHECK(keyboard_spec->requirements.maximum_usages == 98U);
        AOAHID_CHECK(keyboard_spec->requirements.maximum_report_data_bits == 112U);
    }

    std::vector<std::uint8_t> raw_descriptor{
        0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x15U, 0x00U, 0x25U, 0x01U, 0x75U, 0x01U, 0x95U,
        0x01U,
        // First field declares 20001 Local Usages without expanding them.
        0x1AU, 0x00U, 0x00U, 0x2AU, 0x20U, 0x4EU, 0x81U, 0x02U};
    raw_descriptor.reserve(raw_descriptor.size() + (256U * 4U) + 1U);
    for (std::uint32_t field_index = 1U; field_index < 257U; ++field_index) {
        raw_descriptor.push_back(0x09U);
        raw_descriptor.push_back(0x01U);
        raw_descriptor.push_back(0x81U);
        raw_descriptor.push_back(0x02U);
    }
    raw_descriptor.push_back(0xC0U);
    const aoahid_raw_report raw_report{{0U, 0U, {}}, 33U};
    const aoahid_raw_options raw_options{sizeof(aoahid_raw_options),
                                         0U,
                                         raw_descriptor.data(),
                                         raw_descriptor.size(),
                                         &raw_report,
                                         1U,
                                         1U,
                                         0U,
                                         0U};
    aoahid_spec* raw_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_raw(&raw_options, &raw_spec) == AOAHID_OK);
    AOAHID_CHECK(raw_spec != nullptr);
    if (raw_spec != nullptr) {
        AOAHID_CHECK(raw_spec->requirements.maximum_fields_per_report == 257U);
        AOAHID_CHECK(raw_spec->requirements.maximum_global_stack_depth == 0U);
        AOAHID_CHECK(raw_spec->requirements.maximum_report_size_bits == 1U);
        AOAHID_CHECK(raw_spec->requirements.maximum_usages == 20001U);
        AOAHID_CHECK(raw_spec->requirements.maximum_report_data_bits == 257U);
    }

    const std::array<std::uint8_t, 29U> stack_descriptor{
        0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0xA4U, 0xA4U, 0xA4U, 0xA4U,
        0xA4U, 0xB4U, 0xB4U, 0xB4U, 0xB4U, 0xB4U, 0x15U, 0x00U, 0x25U, 0x01U,
        0x75U, 0x01U, 0x95U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U};
    const aoahid_raw_report stack_report{{0U, 0U, {}}, 1U};
    const aoahid_raw_options stack_options{sizeof(aoahid_raw_options),
                                           0U,
                                           stack_descriptor.data(),
                                           stack_descriptor.size(),
                                           &stack_report,
                                           1U,
                                           1U,
                                           0U,
                                           0U};
    aoahid_spec* stack_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_raw(&stack_options, &stack_spec) == AOAHID_OK);
    AOAHID_CHECK(stack_spec != nullptr);
    if (stack_spec != nullptr) {
        AOAHID_CHECK(stack_spec->requirements.maximum_global_stack_depth == 5U);
        AOAHID_CHECK(stack_spec->requirements.maximum_report_size_bits == 1U);
        AOAHID_CHECK(stack_spec->requirements.maximum_report_data_bits == 1U);
    }

    const std::array<std::uint8_t, 24U> report_size_descriptor{
        0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x77U, 0x01U, 0x01U, 0x00U, 0x00U, 0x15U,
        0x00U, 0x25U, 0x01U, 0x75U, 0x01U, 0x95U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U};
    const aoahid_raw_report report_size_report{{0U, 0U, {}}, 1U};
    const aoahid_raw_options report_size_options{sizeof(aoahid_raw_options),
                                                 0U,
                                                 report_size_descriptor.data(),
                                                 report_size_descriptor.size(),
                                                 &report_size_report,
                                                 1U,
                                                 1U,
                                                 0U,
                                                 0U};
    aoahid_spec* report_size_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_raw(&report_size_options, &report_size_spec) == AOAHID_OK);
    AOAHID_CHECK(report_size_spec != nullptr);
    if (report_size_spec != nullptr) {
        AOAHID_CHECK(report_size_spec->requirements.maximum_report_size_bits == 257U);
        AOAHID_CHECK(report_size_spec->requirements.maximum_report_data_bits == 1U);
    }

    const std::array<std::uint8_t, 20U> large_report_descriptor{
        0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x15U, 0x00U, 0x25U, 0x01U,
        0x75U, 0x20U, 0x96U, 0x00U, 0x08U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U};
    const aoahid_raw_report large_report{{0U, 0U, {}}, 8192U};
    const aoahid_raw_options large_report_options{sizeof(aoahid_raw_options),
                                                  0U,
                                                  large_report_descriptor.data(),
                                                  large_report_descriptor.size(),
                                                  &large_report,
                                                  1U,
                                                  1U,
                                                  0U,
                                                  0U};
    aoahid_spec* large_report_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_raw(&large_report_options, &large_report_spec) == AOAHID_OK);
    AOAHID_CHECK(large_report_spec != nullptr);
    if (large_report_spec != nullptr) {
        AOAHID_CHECK(large_report_spec->requirements.maximum_report_size_bits == 32U);
        AOAHID_CHECK(large_report_spec->requirements.maximum_report_data_bits == 65536U);
    }

    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    const auto reject_before_registration =
        [&](aoahid_spec* spec, const std::uint32_t fields_policy, const std::uint32_t stack_policy,
            const std::uint64_t usages_policy, const std::uint64_t report_bits_policy,
            const std::uint32_t report_size_policy, const char* expected_field) {
            aoahid_device_options options = public_device_options();
            options.aoa_descriptor_wire_policy_bytes = 4096U;
            options.linux_hid.descriptor_bytes = 4096U;
            options.linux_hid.fields_per_report = fields_policy;
            options.linux_hid.global_stack_depth = stack_policy;
            options.linux_hid.usages = usages_policy;
            options.linux_hid.report_data_bits = report_bits_policy;
            options.linux_hid.report_size_bits = report_size_policy;
            options.maximum_report_bytes = 8192U;
            options.target_ep0_data_policy_bytes = 8192U;
            options.host_control_buffer_policy_bytes = 8192U;
            aoahid_device* device = nullptr;
            AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) == AOAHID_OK);
            AOAHID_CHECK(device != nullptr);
            if (device == nullptr)
                return;
            AOAHID_CHECK(device->linux_hid.fields_per_report == fields_policy);
            AOAHID_CHECK(device->linux_hid.global_stack_depth == stack_policy);
            AOAHID_CHECK(device->linux_hid.usages == usages_policy);
            AOAHID_CHECK(device->linux_hid.report_data_bits == report_bits_policy);
            AOAHID_CHECK(device->linux_hid.report_size_bits == report_size_policy);
            aoahid_node* node = nullptr;
            AOAHID_CHECK(aoahid_node_open(device, spec, &node_options, kControlTimeoutMs, &node) ==
                         AOAHID_ERR_OVERFLOW);
            AOAHID_CHECK(node == nullptr);
            AOAHID_CHECK(aoahid_last_error()->field != nullptr);
            if (aoahid_last_error()->field != nullptr) {
                AOAHID_CHECK(std::string_view(aoahid_last_error()->field) == expected_field);
            }
            AOAHID_CHECK(controls_for(54U).empty());
            AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, kDrainTimeoutMs) ==
                         AOAHID_OK);
        };

    if (keyboard_spec != nullptr) {
        reject_before_registration(keyboard_spec, 1U, 4U, 1000U, 65528U, 256U,
                                   "spec.requirements.maximum_fields_per_report");
        reject_before_registration(keyboard_spec, 10U, 4U, 97U, 65528U, 256U,
                                   "spec.requirements.maximum_usages");
    }
    if (raw_spec != nullptr) {
        reject_before_registration(raw_spec, 256U, 4U, 50000U, 65528U, 256U,
                                   "spec.requirements.maximum_fields_per_report");
        reject_before_registration(raw_spec, 1000U, 4U, 12288U, 65528U, 256U,
                                   "spec.requirements.maximum_usages");
    }

    if (stack_spec != nullptr) {
        reject_before_registration(stack_spec, 256U, 4U, 12288U, 65528U, 256U,
                                   "spec.requirements.maximum_global_stack_depth");
    }
    if (report_size_spec != nullptr) {
        reject_before_registration(report_size_spec, 256U, 4U, 12288U, 65528U, 256U,
                                   "spec.requirements.maximum_report_size_bits");
    }
    if (large_report_spec != nullptr) {
        reject_before_registration(large_report_spec, 256U, 4U, 12288U, 65528U, 256U,
                                   "spec.requirements.maximum_report_data_bits");
    }

    aoahid_spec_release(large_report_spec);
    aoahid_spec_release(report_size_spec);
    aoahid_spec_release(stack_spec);
    aoahid_spec_release(raw_spec);
    aoahid_spec_release(keyboard_spec);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

} // namespace transport_sim
