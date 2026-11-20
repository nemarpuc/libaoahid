// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * The gamepad profile (HID Usage Tables: Generic Desktop Game Pad): its Spec factory.
 */

#include "profiles/spec_support.hpp"

using namespace aoa::spec_build;

extern "C" {

constexpr aoahid_physical_properties kAndroidHatDegrees{1U, 0, 315, 0, 0x14U};

static aoahid_result create_controller_spec(const aoahid_gamepad_options* options,
                                            aoahid_spec** out_spec) {
    if (options == nullptr ||
        !aoa::detail::valid_struct(options, options == nullptr ? 0U : options->struct_size,
                                   static_cast<std::uint32_t>(sizeof(*options)),
                                   "gamepad_options")) {
        return AOAHID_ERR_PARAM;
    }
    if (options->reserved != 0U || !validate_report_id(options->report_id, "gamepad.report_id") ||
        options->axes == nullptr || options->axis_count < 2U || options->button_count == 0U ||
        options->button_count > 65535U || options->button_usage_minimum == 0U ||
        (options->dpad_representation != AOAHID_DPAD_NONE &&
         options->dpad_representation != AOAHID_DPAD_HAT &&
         options->dpad_representation != AOAHID_DPAD_BUTTONS)) {
        set_error(AOAHID_ERR_PARAM, "gamepad",
                  "A gamepad requires explicit axes, at least one button, and valid flags.");
        return AOAHID_ERR_PARAM;
    }
    if (options->axis_count > std::numeric_limits<std::uint16_t>::max()) {
        set_error(AOAHID_ERR_OVERFLOW, "gamepad.axis_count",
                  "The generated layout cannot index more than 65535 controller axes.");
        return AOAHID_ERR_OVERFLOW;
    }
    if (static_cast<std::uint32_t>(options->button_usage_minimum) + options->button_count - 1U >
        std::numeric_limits<std::uint16_t>::max()) {
        set_error(AOAHID_ERR_OVERFLOW, "gamepad.button_usage_minimum",
                  "The declared button Usage range exceeds the 16-bit Usage value space.");
        return AOAHID_ERR_OVERFLOW;
    }
    if (options->dpad_representation == AOAHID_DPAD_BUTTONS &&
        options->button_count >
            static_cast<std::uint32_t>(std::numeric_limits<std::uint16_t>::max()) - 3U) {
        set_error(AOAHID_ERR_OVERFLOW, "gamepad.button_count",
                  "Four D-pad fields must fit the layout's 16-bit field-instance space.");
        return AOAHID_ERR_OVERFLOW;
    }
    bool has_x = false;
    bool has_y = false;
    aoa::detail::GamepadConfig config{};
    config.options = *options;
    for (std::size_t index = 0; index < options->axis_count; ++index) {
        std::uint16_t expected_page = 0U;
        std::uint16_t expected_usage = 0U;
        if (!controller_axis_usage(options->axes[index].role, &expected_page, &expected_usage) ||
            options->axes[index].usage_page != expected_page ||
            options->axes[index].usage != expected_usage) {
            set_error(AOAHID_ERR_PARAM, "gamepad.axes.role",
                      "Each axis role must match its audited HUT 1.7 Usage Page and Usage.");
            return AOAHID_ERR_PARAM;
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (options->axes[previous].role == options->axes[index].role) {
                set_error(AOAHID_ERR_PARAM, "gamepad.axes.role",
                          "Each controller axis role may be declared only once.");
                return AOAHID_ERR_PARAM;
            }
        }
        if (!validate_field(options->axes[index].value, "gamepad.axes.value") ||
            options->axes[index].neutral_value < options->axes[index].value.logical_minimum ||
            options->axes[index].neutral_value > options->axes[index].value.logical_maximum ||
            options->axes[index].usage_page == 0U || options->axes[index].usage == 0U ||
            options->axes[index].expected_linux_code == nullptr ||
            options->axes[index].expected_android_axis == nullptr ||
            options->axes[index].expected_linux_code[0] == '\0' ||
            options->axes[index].expected_android_axis[0] == '\0') {
            set_error(
                AOAHID_ERR_UNSET_FIELD, "gamepad.axes",
                "Every axis requires Usage, range, width, Linux code, and Android axis evidence.");
            return AOAHID_ERR_UNSET_FIELD;
        }
        if (!controller_axis_domain_matches_usage(options->axes[index])) {
            set_error(AOAHID_ERR_PARAM, "gamepad.axes.value",
                      "Simulation Accelerator, Brake, and Throttle require a zero-to-positive "
                      "range with zero neutral; Steering and Rudder require a signed range "
                      "around zero with zero neutral.");
            return AOAHID_ERR_PARAM;
        }
        has_x = has_x || options->axes[index].role == AOAHID_AXIS_X;
        has_y = has_y || options->axes[index].role == AOAHID_AXIS_Y;
        aoa::detail::GamepadAxisConfig axis{};
        axis.axis = options->axes[index];
        axis.expected_linux_code = options->axes[index].expected_linux_code;
        axis.expected_android_axis = options->axes[index].expected_android_axis;
        axis.axis.expected_linux_code = nullptr;
        axis.axis.expected_android_axis = nullptr;
        config.axes.push_back(std::move(axis));
    }
    if (!has_x || !has_y) {
        set_error(AOAHID_ERR_PARAM, "gamepad.axes",
                  "The portable candidate requires explicitly declared X and Y axes.");
        return AOAHID_ERR_PARAM;
    }
    if (options->dpad_representation == AOAHID_DPAD_HAT) {
        if (options->hat_logical_minimum != 0 || options->hat_logical_maximum != 7 ||
            options->hat_bit_width != 4U ||
            !has_null_encoding(NullEncodingRange{options->hat_logical_minimum,
                                                 options->hat_logical_maximum,
                                                 options->hat_bit_width})) {
            set_error(AOAHID_ERR_PARAM, "gamepad.hat",
                      "The Android portable-candidate Hat must explicitly select logical 0..7 "
                      "in four bits and leave an encoding for Null State.");
            return AOAHID_ERR_PARAM;
        }
    } else if (options->hat_logical_minimum != 0 || options->hat_logical_maximum != 0 ||
               options->hat_bit_width != 0U) {
        set_error(AOAHID_ERR_PARAM, "gamepad.hat",
                  "Hat range fields must be canonical zero unless the D-pad is a Hat Switch.");
        return AOAHID_ERR_PARAM;
    }
    config.options.axes = nullptr;
    // Android 17 CDD §7.2.6.1 lists A/B/X/Y as Button usages 1, 2, 4,
    // and 5 and defines its directional mapping through the canonical Hat.
    // A contiguous Button range starting at 1 therefore needs at least five
    // entries before this descriptor shape is a portable candidate.
    const bool has_android_abxy =
        options->button_usage_minimum == 1U && options->button_count >= 5U;
    const aoahid_android_status android_status =
        options->dpad_representation != AOAHID_DPAD_HAT || !has_android_abxy
            ? AOAHID_ANDROID_CONDITIONAL
            : AOAHID_ANDROID_PORTABLE_CANDIDATE;
    const BatteryRequest battery{options->enable_battery, options->battery, options->report_id};
    if (!validate_battery(battery, kGamepadBatteryNames)) {
        return AOAHID_ERR_PARAM;
    }
    return build_generated_spec(
        GeneratedSpecIdentity{AOAHID_PROFILE_GAMEPAD, android_status}, std::move(config), battery,
        [=](DescriptorBuilder& builder) {
            if (!builder.begin_application(aoa::hid::usage::page_generic_desktop,
                                           aoa::hid::usage::gamepad) ||
                !builder.set_report_id(options->report_id.enabled == 1U,
                                       options->report_id.value) ||
                !builder.variable_range(aoa::hid::usage::page_button, options->button_usage_minimum,
                                        static_cast<std::uint16_t>(options->button_usage_minimum +
                                                                   options->button_count - 1U),
                                        1U, FieldSemantic::buttons)) {
                return false;
            }
            pad_report(builder);
            if (options->dpad_representation == AOAHID_DPAD_HAT &&
                !builder.variable(aoa::hid::usage::page_generic_desktop,
                                  aoa::hid::usage::hat_switch, options->hat_logical_minimum,
                                  options->hat_logical_maximum,
                                  static_cast<std::uint8_t>(options->hat_bit_width), false, true,
                                  FieldSemantic::hat, 0U, false, &kAndroidHatDegrees)) {
                return false;
            }
            if (options->dpad_representation == AOAHID_DPAD_BUTTONS) {
                constexpr std::uint16_t usages[] = {
                    aoa::hid::usage::dpad_up, aoa::hid::usage::dpad_down,
                    aoa::hid::usage::dpad_right, aoa::hid::usage::dpad_left};
                for (std::size_t index = 0U; index < 4U; ++index) {
                    if (!builder.variable(aoa::hid::usage::page_generic_desktop, usages[index], 0,
                                          1, 1U, false, false, FieldSemantic::buttons,
                                          static_cast<std::uint16_t>(options->button_count + index),
                                          true)) {
                        return false;
                    }
                }
            }
            // The Hat Switch and the raw D-pad OOC bits above are 4 bits wide
            // and are not byte-aligned on their own; without realigning here,
            // an axis width in HUT 1.7's full 1-32 bit range (see LIMITS.md)
            // could start at a nonzero bit offset and cross a fifth byte,
            // which HID 1.11 section 8.4 forbids for a single field.
            pad_report(builder);
            for (std::size_t index = 0; index < options->axis_count; ++index) {
                const aoahid_gamepad_axis_option& axis = options->axes[index];
                if (!builder.variable(
                        axis.usage_page, axis.usage, axis.value.logical_minimum,
                        axis.value.logical_maximum, static_cast<std::uint8_t>(axis.value.bit_width),
                        false, false, FieldSemantic::gamepad_axis,
                        static_cast<std::uint16_t>(index), false, &axis.value.physical)) {
                    return false;
                }
                // Each axis has its own caller-declared width (1-32 bits, see LIMITS.md). A
                // width that is not a multiple of eight leaves the next axis at a nonzero bit
                // offset; realigning after each axis keeps that drift from ever compounding into
                // a fifth-byte span two or more axes in, regardless of the declared width.
                pad_report(builder);
            }
            pad_report(builder);
            return builder.end_collection();
        },
        out_spec);
}

static aoahid_result aoahid_spec_create_gamepad_impl(const aoahid_gamepad_options* options,
                                                     aoahid_spec** out_spec) {
    aoa::detail::clear_error();
    return create_controller_spec(options, out_spec);
}

aoahid_result AOAHID_CALL aoahid_spec_create_gamepad(const aoahid_gamepad_options* options,
                                                     aoahid_spec** out_spec) {
    return abi_spec_factory(out_spec, "spec.create_gamepad",
                            [&] { return aoahid_spec_create_gamepad_impl(options, out_spec); });
}

} // extern "C"
