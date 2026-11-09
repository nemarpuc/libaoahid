// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Gamepad: the hat switch domain, D-pad representations and extended axes. */
#include "unit/profile_support.hpp"

namespace profile_test {

void test_hat_domain() {
    std::array<aoahid_gamepad_axis_option, 2> axes{};
    axes[0] = {AOAHID_AXIS_X, 0x01U, 0x30U, {-127, 127, 8U, {}}, 0, "ABS_X", "AXIS_X"};
    axes[1] = {AOAHID_AXIS_Y, 0x01U, 0x31U, {-127, 127, 8U, {}}, 0, "ABS_Y", "AXIS_Y"};
    aoahid_gamepad_options options{};
    options.struct_size = static_cast<std::uint32_t>(sizeof(options));
    options.report_id = {0U, 0U, {0U, 0U, 0U}};
    options.axes = axes.data();
    options.axis_count = axes.size();
    options.button_count = 1U;
    options.button_usage_minimum = 1U;
    options.dpad_representation = AOAHID_DPAD_HAT;
    options.hat_logical_minimum = 0;
    options.hat_logical_maximum = 15;
    options.hat_bit_width = 4U;
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_gamepad(&options, &spec) == AOAHID_ERR_PARAM);
    options.hat_logical_maximum = 7;
    check_battery_variant(options, &aoahid_spec_create_gamepad);
    AOAHID_CHECK(aoahid_spec_create_gamepad(&options, &spec) == AOAHID_OK);
    if (spec != nullptr) {
        aoahid_capability_manifest manifest{};
        manifest.struct_size = static_cast<std::uint32_t>(sizeof(manifest));
        AOAHID_CHECK(aoahid_spec_manifest(spec, &manifest) == AOAHID_OK);
        AOAHID_CHECK(manifest.android_status == AOAHID_ANDROID_CONDITIONAL);
        aoahid_node node{};
        node.spec = spec;
        aoa::detail::GamepadState state_value{};
        state_value.axes.resize(axes.size());
        state_value.buttons.resize(options.button_count);
        state_value.button_transitions.resize(options.button_count);
        node.state = std::move(state_value);
        std::array<std::uint8_t, 32U> report{};
        std::size_t length = 0U;
        AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                     AOAHID_OK);
        const auto* hat = field(spec, aoa::hid::FieldSemantic::hat);
        AOAHID_CHECK(hat != nullptr && extract(report.data(), *hat) == 15U);
    }
    aoahid_spec_release(spec);

    axes[0].usage_page = 0x02U;
    axes[0].usage = 0xC4U;
    spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_gamepad(&options, &spec) == AOAHID_ERR_PARAM);
    axes[0].usage_page = 0x01U;
    axes[0].usage = 0x30U;

    axes[1].role = AOAHID_AXIS_X;
    axes[1].usage = 0x30U;
    AOAHID_CHECK(aoahid_spec_create_gamepad(&options, &spec) == AOAHID_ERR_PARAM);
    axes[1].role = AOAHID_AXIS_Y;
    axes[1].usage = 0x31U;

    axes[1].expected_linux_code = "";
    AOAHID_CHECK(aoahid_spec_create_gamepad(&options, &spec) == AOAHID_ERR_UNSET_FIELD);
}

void test_controller_dpad_and_extended_axes() {
    std::array<aoahid_gamepad_axis_option, 6U> axes{
        aoahid_gamepad_axis_option{
            AOAHID_AXIS_X, 0x01U, 0x30U, {-127, 127, 8U, {}}, 0, "ABS_X", "AXIS_X"},
        aoahid_gamepad_axis_option{
            AOAHID_AXIS_Y, 0x01U, 0x31U, {-127, 127, 8U, {}}, 0, "ABS_Y", "AXIS_Y"},
        aoahid_gamepad_axis_option{
            AOAHID_AXIS_DIAL, 0x01U, 0x37U, {-127, 127, 8U, {}}, 0, "TEST", "TEST"},
        aoahid_gamepad_axis_option{
            AOAHID_AXIS_WHEEL, 0x01U, 0x38U, {-127, 127, 8U, {}}, 0, "TEST", "TEST"},
        aoahid_gamepad_axis_option{
            AOAHID_AXIS_SIMULATION_RUDDER, 0x02U, 0xBAU, {-127, 127, 8U, {}}, 0, "TEST", "TEST"},
        aoahid_gamepad_axis_option{
            AOAHID_AXIS_SIMULATION_THROTTLE, 0x02U, 0xBBU, {0, 255, 8U, {}}, 0, "TEST", "TEST"}};
    aoahid_gamepad_options options{};
    options.struct_size = static_cast<std::uint32_t>(sizeof(options));
    options.axes = axes.data();
    options.axis_count = axes.size();
    options.button_count = 1U;
    options.button_usage_minimum = 1U;
    options.dpad_representation = AOAHID_DPAD_BUTTONS;

    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_gamepad(&options, &spec) == AOAHID_OK);
    if (spec == nullptr)
        return;
    aoahid_capability_manifest raw_dpad_manifest{};
    raw_dpad_manifest.struct_size = static_cast<std::uint32_t>(sizeof(raw_dpad_manifest));
    AOAHID_CHECK(aoahid_spec_manifest(spec, &raw_dpad_manifest) == AOAHID_OK);
    AOAHID_CHECK(raw_dpad_manifest.android_status == AOAHID_ANDROID_CONDITIONAL);
    AOAHID_CHECK(count_item(spec->descriptor, 0x09U, 0x90U) == 1U);
    AOAHID_CHECK(count_item(spec->descriptor, 0x09U, 0x91U) == 1U);
    AOAHID_CHECK(count_item(spec->descriptor, 0x09U, 0x92U) == 1U);
    AOAHID_CHECK(count_item(spec->descriptor, 0x09U, 0x93U) == 1U);
    AOAHID_CHECK(count_item(spec->descriptor, 0x81U, 0x22U) == 4U);

    aoahid_node node{};
    node.spec = spec;
    aoa::detail::GamepadState gamepad{};
    gamepad.axes.resize(axes.size());
    gamepad.buttons.resize(options.button_count);
    gamepad.button_transitions.resize(options.button_count);
    node.state = std::move(gamepad);
    node.dirty = false;
    AOAHID_CHECK(aoahid_gamepad_dpad(&node, 1U, 0U, 1U, 0U) == AOAHID_OK);
    AOAHID_CHECK(node.dirty);
    std::array<std::uint8_t, 64U> report{};
    std::size_t length = 0U;
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    const auto* up = field(spec, aoa::hid::FieldSemantic::buttons, 1U);
    const auto* down = field(spec, aoa::hid::FieldSemantic::buttons, 2U);
    const auto* right = field(spec, aoa::hid::FieldSemantic::buttons, 3U);
    const auto* left = field(spec, aoa::hid::FieldSemantic::buttons, 4U);
    AOAHID_CHECK(up != nullptr && extract(report.data(), *up) == 1U);
    AOAHID_CHECK(down != nullptr && extract(report.data(), *down) == 0U);
    AOAHID_CHECK(right != nullptr && extract(report.data(), *right) == 1U);
    AOAHID_CHECK(left != nullptr && extract(report.data(), *left) == 0U);
    AOAHID_CHECK(aoahid_gamepad_dpad(&node, 0U, 1U, 0U, 1U) == AOAHID_ERR_BUSY);
    node.dirty = false;
    AOAHID_CHECK(aoahid_gamepad_dpad(&node, 1U, 0U, 1U, 0U) == AOAHID_OK);
    AOAHID_CHECK(!node.dirty);
    aoa::detail::transfer_complete(&node, AOAHID_OK, 0);
    AOAHID_CHECK(aoahid_gamepad_dpad(&node, 0U, 1U, 0U, 1U) == AOAHID_OK);
    AOAHID_CHECK(node.dirty);
    node.dirty = false;
    AOAHID_CHECK(aoahid_gamepad_axis(&node, 0U, 0) == AOAHID_OK);
    AOAHID_CHECK(!node.dirty);
    aoahid_spec_release(spec);
    spec = nullptr;

    options.button_count = 4U;
    options.dpad_representation = AOAHID_DPAD_HAT;
    options.hat_logical_minimum = 0;
    options.hat_logical_maximum = 7;
    options.hat_bit_width = 4U;
    AOAHID_CHECK(aoahid_spec_create_gamepad(&options, &spec) == AOAHID_OK);
    if (spec != nullptr) {
        aoahid_capability_manifest four_button_manifest{};
        four_button_manifest.struct_size = static_cast<std::uint32_t>(sizeof(four_button_manifest));
        AOAHID_CHECK(aoahid_spec_manifest(spec, &four_button_manifest) == AOAHID_OK);
        AOAHID_CHECK(four_button_manifest.android_status == AOAHID_ANDROID_CONDITIONAL);
        aoahid_spec_release(spec);
        spec = nullptr;
    }
    options.button_count = 5U;
    AOAHID_CHECK(aoahid_spec_create_gamepad(&options, &spec) == AOAHID_OK);
    if (spec != nullptr) {
        aoahid_capability_manifest manifest{};
        manifest.struct_size = static_cast<std::uint32_t>(sizeof(manifest));
        AOAHID_CHECK(aoahid_spec_manifest(spec, &manifest) == AOAHID_OK);
        AOAHID_CHECK(manifest.android_status == AOAHID_ANDROID_PORTABLE_CANDIDATE);
        const auto* hat = field(spec, aoa::hid::FieldSemantic::hat);
        AOAHID_CHECK(hat != nullptr && hat->physical.enabled == 1U && hat->physical.minimum == 0 &&
                     hat->physical.maximum == 315 && hat->physical.unit_exponent == 0 &&
                     hat->physical.unit == 0x14U);
        aoahid_node hat_node{};
        hat_node.spec = spec;
        aoa::detail::GamepadState hat_state{};
        hat_state.axes.resize(axes.size());
        hat_state.buttons.resize(options.button_count);
        hat_state.button_transitions.resize(options.button_count);
        hat_node.state = std::move(hat_state);
        struct DirectionCase {
            std::uint32_t up;
            std::uint32_t down;
            std::uint32_t right;
            std::uint32_t left;
            std::uint32_t wire;
        };
        const std::array<DirectionCase, 9U> directions{
            DirectionCase{0U, 0U, 0U, 0U, 15U}, DirectionCase{1U, 0U, 0U, 0U, 0U},
            DirectionCase{1U, 0U, 1U, 0U, 1U},  DirectionCase{0U, 0U, 1U, 0U, 2U},
            DirectionCase{0U, 1U, 1U, 0U, 3U},  DirectionCase{0U, 1U, 0U, 0U, 4U},
            DirectionCase{0U, 1U, 0U, 1U, 5U},  DirectionCase{0U, 0U, 0U, 1U, 6U},
            DirectionCase{1U, 0U, 0U, 1U, 7U}};
        for (const auto& direction : directions) {
            AOAHID_CHECK(aoahid_gamepad_dpad(&hat_node, direction.up, direction.down,
                                             direction.right, direction.left) == AOAHID_OK);
            report.fill(0U);
            AOAHID_CHECK(aoa::detail::serialize_node(&hat_node, report.data(), report.size(),
                                                     &length) == AOAHID_OK);
            AOAHID_CHECK(hat != nullptr && extract(report.data(), *hat) == direction.wire);
            aoa::detail::transfer_complete(&hat_node, AOAHID_OK, 0);
            hat_node.dirty = false;
        }
        AOAHID_CHECK(aoahid_gamepad_dpad(&hat_node, 1U, 1U, 0U, 0U) == AOAHID_ERR_PARAM);
        AOAHID_CHECK(aoahid_gamepad_dpad(&hat_node, 0U, 0U, 1U, 1U) == AOAHID_ERR_PARAM);
        report.fill(0U);
        AOAHID_CHECK(aoa::detail::serialize_node(&hat_node, report.data(), report.size(),
                                                 &length) == AOAHID_OK);
        AOAHID_CHECK(hat != nullptr && extract(report.data(), *hat) == 7U);
        AOAHID_CHECK(!hat_node.dirty);
        aoahid_spec_release(spec);
        spec = nullptr;
    }

    axes[4].neutral_value = 1;
    AOAHID_CHECK(aoahid_spec_create_gamepad(&options, &spec) == AOAHID_ERR_PARAM);
    axes[4].neutral_value = 0;
    axes[4].value = {0, 127, 8U, {}};
    AOAHID_CHECK(aoahid_spec_create_gamepad(&options, &spec) == AOAHID_ERR_PARAM);
    axes[4].value = {-127, 127, 8U, {}};
    axes[5].neutral_value = 1;
    AOAHID_CHECK(aoahid_spec_create_gamepad(&options, &spec) == AOAHID_ERR_PARAM);
    axes[5].neutral_value = 0;
    axes[5].value = {-1, 255, 16U, {}};
    AOAHID_CHECK(aoahid_spec_create_gamepad(&options, &spec) == AOAHID_ERR_PARAM);
    axes[5].value = {0, 255, 8U, {}};

    options.dpad_representation = AOAHID_DPAD_NONE;
    options.hat_logical_minimum = 0;
    options.hat_logical_maximum = 0;
    options.hat_bit_width = 4U;
    AOAHID_CHECK(aoahid_spec_create_gamepad(&options, &spec) == AOAHID_ERR_PARAM);
}

} // namespace profile_test
