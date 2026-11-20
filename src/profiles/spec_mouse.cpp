// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * The mouse profile (HID Usage Tables: Generic Desktop Mouse): its Spec factory.
 */

#include "profiles/spec_support.hpp"

using namespace aoa::spec_build;

extern "C" {

static aoahid_result aoahid_spec_create_mouse_impl(const aoahid_mouse_options* options,
                                                   aoahid_spec** out_spec) {
    aoa::detail::clear_error();
    if (options == nullptr ||
        !aoa::detail::valid_struct(options, options == nullptr ? 0U : options->struct_size,
                                   static_cast<std::uint32_t>(sizeof(*options)), "mouse_options")) {
        return AOAHID_ERR_PARAM;
    }
    if (options->reserved != 0U || !validate_report_id(options->report_id, "mouse.report_id") ||
        !validate_field(options->x, "mouse.x") || !validate_field(options->y, "mouse.y") ||
        options->x.logical_minimum >= 0 || options->x.logical_maximum <= 0 ||
        options->y.logical_minimum >= 0 || options->y.logical_maximum <= 0 ||
        options->button_count == 0U || options->button_count > 65535U ||
        !validate_option_boolean(options->enable_wheel, "mouse.enable_wheel") ||
        !validate_option_boolean(options->enable_pan, "mouse.enable_pan")) {
        set_error(AOAHID_ERR_PARAM, "mouse_options",
                  "The mouse options require valid relative X/Y fields, at least one button, valid "
                  "flags, and zero reserved data.");
        return AOAHID_ERR_PARAM;
    }
    if ((options->enable_wheel == 1U && !validate_field(options->wheel, "mouse.wheel")) ||
        (options->enable_pan == 1U && !validate_field(options->pan, "mouse.pan"))) {
        // validate_field recorded the result (UNSET_FIELD for a zero width); return that one.
        return aoahid_last_error()->code;
    }
    if ((options->enable_wheel == 0U && !field_is_zero(options->wheel)) ||
        (options->enable_pan == 0U && !field_is_zero(options->pan))) {
        set_error(AOAHID_ERR_PARAM, "mouse.disabled_fields",
                  "Disabled wheel and pan declarations must be canonical zero.");
        return AOAHID_ERR_PARAM;
    }
    if ((options->enable_wheel == 1U &&
         (options->wheel.logical_minimum >= 0 || options->wheel.logical_maximum <= 0)) ||
        (options->enable_pan == 1U &&
         (options->pan.logical_minimum >= 0 || options->pan.logical_maximum <= 0))) {
        set_error(AOAHID_ERR_PARAM, "mouse.scroll_range",
                  "Portable relative wheel and pan ranges must represent negative, zero, and "
                  "positive deltas.");
        return AOAHID_ERR_PARAM;
    }
    aoa::detail::MouseConfig config{};
    config.options = *options;
    const BatteryRequest battery{options->enable_battery, options->battery, options->report_id};
    if (!validate_battery(battery, kMouseBatteryNames)) {
        return AOAHID_ERR_PARAM;
    }
    return build_generated_spec(
        GeneratedSpecIdentity{AOAHID_PROFILE_MOUSE, AOAHID_ANDROID_PORTABLE_CANDIDATE}, config,
        battery,
        [=](DescriptorBuilder& builder) {
            if (!builder.begin_application(aoa::hid::usage::page_generic_desktop,
                                           aoa::hid::usage::mouse) ||
                !builder.set_report_id(options->report_id.enabled == 1U,
                                       options->report_id.value) ||
                !builder.begin_physical(aoa::hid::usage::page_generic_desktop,
                                        aoa::hid::usage::pointer) ||
                !builder.variable_range(aoa::hid::usage::page_button, 1U,
                                        static_cast<std::uint16_t>(options->button_count), 1U,
                                        FieldSemantic::buttons)) {
                return false;
            }
            pad_report(builder);
            if (!builder.variable(aoa::hid::usage::page_generic_desktop, aoa::hid::usage::x,
                                  options->x.logical_minimum, options->x.logical_maximum,
                                  static_cast<std::uint8_t>(options->x.bit_width), true, false,
                                  FieldSemantic::x, 0U, false, &options->x.physical) ||
                !builder.variable(aoa::hid::usage::page_generic_desktop, aoa::hid::usage::y,
                                  options->y.logical_minimum, options->y.logical_maximum,
                                  static_cast<std::uint8_t>(options->y.bit_width), true, false,
                                  FieldSemantic::y, 0U, false, &options->y.physical)) {
                return false;
            }
            // X and Y widths are caller-declared (1-32 bits, see LIMITS.md);
            // realign to a byte boundary before Wheel/Pan so a wide,
            // misaligned field can never cross a fifth byte (HID 1.11 8.4).
            pad_report(builder);
            if (options->enable_wheel == 1U &&
                !builder.variable(aoa::hid::usage::page_generic_desktop, aoa::hid::usage::wheel,
                                  options->wheel.logical_minimum, options->wheel.logical_maximum,
                                  static_cast<std::uint8_t>(options->wheel.bit_width), true, false,
                                  FieldSemantic::wheel, 0U, false, &options->wheel.physical)) {
                return false;
            }
            pad_report(builder);
            if (options->enable_pan == 1U &&
                !builder.variable(aoa::hid::usage::page_consumer, aoa::hid::usage::ac_pan,
                                  options->pan.logical_minimum, options->pan.logical_maximum,
                                  static_cast<std::uint8_t>(options->pan.bit_width), true, false,
                                  FieldSemantic::pan, 0U, false, &options->pan.physical)) {
                return false;
            }
            pad_report(builder);
            return builder.end_collection() && builder.end_collection();
        },
        out_spec);
}

aoahid_result AOAHID_CALL aoahid_spec_create_mouse(const aoahid_mouse_options* options,
                                                   aoahid_spec** out_spec) {
    return abi_spec_factory(out_spec, "spec.create_mouse",
                            [&] { return aoahid_spec_create_mouse_impl(options, out_spec); });
}

} // extern "C"
