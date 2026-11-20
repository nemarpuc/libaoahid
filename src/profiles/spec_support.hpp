// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
#pragma once

/*
 * What the Spec factories share: the checks of the caller's fields, the battery report any
 * generated profile can carry, build_generated_spec, the usage tables of the toggle and gamepad
 * profiles, and the wrappers that keep C++ exceptions inside the C ABI. No USB I/O, and no
 * device-specific state: a Spec is immutable once made.
 */

#include "api/internal.hpp"
#include "hid/descriptor_builder.hpp"
#include "hid/usages.hpp"
#include "hid/validator.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace aoa::spec_build {

using aoa::detail::set_error;
using aoa::hid::DescriptorBuilder;
using aoa::hid::FieldSemantic;

constexpr std::size_t kAoaDescriptorWireMaximum = 65535U;

inline bool validate_report_id(const aoahid_report_id_option& option, const char* field) noexcept {
    if (const char* reason = aoa::hid::report_id_option_violation(option)) {
        set_error(AOAHID_ERR_PARAM, field, reason);
        return false;
    }
    return true;
}

inline bool physical_is_zero(const aoahid_physical_properties& physical) noexcept {
    return physical.enabled == 0U && physical.minimum == 0 && physical.maximum == 0 &&
           physical.unit_exponent == 0 && physical.unit == 0U;
}

inline bool physical_units_match(const aoahid_physical_properties& first,
                                 const aoahid_physical_properties& second) noexcept {
    if (first.enabled != second.enabled) {
        return false;
    }
    return first.enabled == 0U ||
           (first.unit == second.unit && first.unit_exponent == second.unit_exponent);
}

inline bool validate_physical(const aoahid_physical_properties& physical,
                              const char* name) noexcept {
    if (!aoa::detail::valid_boolean(physical.enabled)) {
        set_error(AOAHID_ERR_PARAM, name, "physical.enabled must be exactly zero or one.");
        return false;
    }
    if (physical.enabled == 0U) {
        if (!physical_is_zero(physical)) {
            set_error(AOAHID_ERR_PARAM, name,
                      "Disabled physical properties must be canonical zero.");
            return false;
        }
        return true;
    }
    if (physical.maximum <= physical.minimum || physical.unit_exponent < -8 ||
        physical.unit_exponent > 7 || physical.unit == 0U ||
        !aoa::hid::unit_is_valid(physical.unit)) {
        set_error(AOAHID_ERR_PARAM, name,
                  "Enabled physical properties require ordered extents, exponent -8 through 7, and "
                  "a valid nonzero HID Unit.");
        return false;
    }
    return true;
}

inline bool validate_field(const aoahid_integer_field& field, const char* name) noexcept {
    if (!validate_physical(field.physical, name)) {
        return false;
    }
    if (field.bit_width == 0U) {
        set_error(AOAHID_ERR_UNSET_FIELD, name, "bit_width is required and cannot be zero.");
        return false;
    }
    if (field.bit_width > 32U ||
        !aoa::hid::value_fits(field.logical_minimum, field.logical_maximum,
                              static_cast<std::uint8_t>(field.bit_width))) {
        set_error(AOAHID_ERR_PARAM, name,
                  "The logical range must be ordered and fit the declared width and signedness.");
        return false;
    }
    return true;
}

inline bool field_is_zero(const aoahid_integer_field& field) noexcept {
    return field.logical_minimum == 0 && field.logical_maximum == 0 && field.bit_width == 0U &&
           physical_is_zero(field.physical);
}

struct NullEncodingRange {
    std::int32_t minimum;
    std::int32_t maximum;
    std::uint32_t bit_width;
};

inline bool has_null_encoding(const NullEncodingRange range) noexcept {
    if (range.bit_width == 0U || range.bit_width > 32U) {
        return false;
    }
    std::int64_t representable_minimum = 0;
    std::int64_t representable_maximum = 0;
    if (range.minimum < 0 || range.maximum < 0) {
        representable_minimum = range.bit_width == 32U
                                    ? std::numeric_limits<std::int32_t>::min()
                                    : -(std::int64_t{1} << (range.bit_width - 1U));
        representable_maximum = range.bit_width == 32U
                                    ? std::numeric_limits<std::int32_t>::max()
                                    : (std::int64_t{1} << (range.bit_width - 1U)) - 1;
    } else {
        representable_maximum =
            range.bit_width == 32U
                ? static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())
                : (std::int64_t{1} << range.bit_width) - 1;
    }
    return static_cast<std::int64_t>(range.minimum) > representable_minimum ||
           static_cast<std::int64_t>(range.maximum) < representable_maximum;
}

inline bool validate_option_boolean(const std::uint32_t value, const char* field) noexcept {
    if (!aoa::detail::valid_boolean(value)) {
        set_error(AOAHID_ERR_PARAM, field, "The flag must be exactly zero or one.");
        return false;
    }
    return true;
}

inline void pad_report(DescriptorBuilder& builder) {
    const std::size_t padding = (8U - (builder.input_bits() & 7U)) & 7U;
    if (padding != 0U) {
        builder.constant_padding(static_cast<std::uint16_t>(padding));
    }
}

struct GeneratedSpecIdentity {
    aoahid_profile_kind kind;
    aoahid_android_status status;
};

struct BatteryRequest {
    std::uint32_t enabled;
    aoahid_battery_report report;
    aoahid_report_id_option input_report_id;
};

struct BatteryFieldNames {
    const char* enable;
    const char* battery;
    const char* report_id;
    const char* strength;
};

constexpr BatteryFieldNames kKeyboardBatteryNames{"keyboard.enable_battery", "keyboard.battery",
                                                  "keyboard.battery.report_id",
                                                  "keyboard.battery.strength"};
constexpr BatteryFieldNames kMouseBatteryNames{"mouse.enable_battery", "mouse.battery",
                                               "mouse.battery.report_id", "mouse.battery.strength"};
constexpr BatteryFieldNames kToggleBatteryNames{"toggle.enable_battery", "toggle.battery",
                                                "toggle.battery.report_id",
                                                "toggle.battery.strength"};
constexpr BatteryFieldNames kGamepadBatteryNames{"gamepad.enable_battery", "gamepad.battery",
                                                 "gamepad.battery.report_id",
                                                 "gamepad.battery.strength"};
constexpr BatteryFieldNames kTouchscreenBatteryNames{
    "touchscreen.enable_battery", "touchscreen.battery", "touchscreen.battery.report_id",
    "touchscreen.battery.strength"};
constexpr BatteryFieldNames kTouchpadBatteryNames{"touchpad.enable_battery", "touchpad.battery",
                                                  "touchpad.battery.report_id",
                                                  "touchpad.battery.strength"};
constexpr BatteryFieldNames kPenBatteryNames{"pen.enable_battery", "pen.battery",
                                             "pen.battery.report_id", "pen.battery.strength"};
constexpr BatteryFieldNames kDigitizerBatteryNames{"digitizer.enable_battery", "digitizer.battery",
                                                   "digitizer.battery.report_id",
                                                   "digitizer.battery.strength"};

inline bool validate_battery(const BatteryRequest& request,
                             const BatteryFieldNames& names) noexcept {
    if (!validate_option_boolean(request.enabled, names.enable)) {
        return false;
    }
    const aoahid_battery_report& battery = request.report;
    if (request.enabled == 0U) {
        const bool zero =
            battery.report_id.enabled == 0U && battery.report_id.value == 0U &&
            battery.report_id.reserved8[0] == 0U && battery.report_id.reserved8[1] == 0U &&
            battery.report_id.reserved8[2] == 0U && battery.strength.logical_minimum == 0 &&
            battery.strength.logical_maximum == 0 && battery.strength.bit_width == 0U &&
            physical_is_zero(battery.strength.physical);
        if (!zero) {
            set_error(AOAHID_ERR_PARAM, names.battery,
                      "battery must be all zero while enable_battery is zero.");
        }
        return zero;
    }
    if (!validate_report_id(battery.report_id, names.report_id)) {
        return false;
    }
    if (request.input_report_id.enabled != 1U || battery.report_id.enabled != 1U ||
        battery.report_id.value == request.input_report_id.value) {
        set_error(AOAHID_ERR_PARAM, names.report_id,
                  "A battery report needs the profile's Report ID and its own, with different "
                  "values.");
        return false;
    }
    if (!validate_field(battery.strength, names.strength) || battery.strength.logical_minimum < 0 ||
        battery.strength.logical_maximum <= battery.strength.logical_minimum) {
        set_error(AOAHID_ERR_PARAM, names.strength,
                  "Battery Strength requires an ordered nonnegative range that fits its width.");
        return false;
    }
    return true;
}

template <typename Emit>
inline aoahid_result
build_generated_spec(const GeneratedSpecIdentity identity, aoa::detail::SpecConfig config,
                     const BatteryRequest& battery, Emit emit, aoahid_spec** out_spec) {
    if (out_spec == nullptr) {
        set_error(AOAHID_ERR_PARAM, "out_spec", "The output pointer is null.");
        return AOAHID_ERR_PARAM;
    }
    *out_spec = nullptr;
    auto spec = std::make_unique<aoahid_spec>();
    const aoa::hid::BatteryField battery_field{
        battery.report.report_id.value, battery.report.strength.logical_minimum,
        battery.report.strength.logical_maximum,
        static_cast<std::uint8_t>(battery.report.strength.bit_width),
        battery.report.strength.physical};
    const bool has_battery = battery.enabled == 1U;
    DescriptorBuilder counting_builder(nullptr, kAoaDescriptorWireMaximum, &spec->layout);
    if (has_battery) {
        counting_builder.set_battery(battery_field, &spec->battery_layout);
    }
    const bool counted = emit(counting_builder);
    counting_builder.finish();
    if (!counted || !counting_builder.good()) {
        set_error(AOAHID_ERR_OVERFLOW, "spec.descriptor",
                  "Descriptor generation failed or exceeded the AOA wire-length field.");
        return AOAHID_ERR_OVERFLOW;
    }
    spec->requirements = counting_builder.requirements();
    std::vector<std::uint8_t> construction(counting_builder.descriptor_size());
    DescriptorBuilder emitting_builder(construction.data(), construction.size(), nullptr);
    if (has_battery) {
        emitting_builder.set_battery(battery_field, nullptr);
    }
    const bool emitted = emit(emitting_builder);
    emitting_builder.finish();
    if (!emitted || !emitting_builder.good() ||
        emitting_builder.descriptor_size() != construction.size() ||
        emitting_builder.requirements().maximum_fields_per_report !=
            spec->requirements.maximum_fields_per_report ||
        emitting_builder.requirements().maximum_global_stack_depth !=
            spec->requirements.maximum_global_stack_depth ||
        emitting_builder.requirements().maximum_report_size_bits !=
            spec->requirements.maximum_report_size_bits ||
        emitting_builder.requirements().maximum_usages != spec->requirements.maximum_usages ||
        emitting_builder.requirements().maximum_report_data_bits !=
            spec->requirements.maximum_report_data_bits) {
        set_error(AOAHID_ERR_INTERNAL, "spec.descriptor",
                  "The descriptor counting and emission passes produced different results.");
        return AOAHID_ERR_INTERNAL;
    }
    const aoa::hid::ValidationIssue validation =
        aoa::hid::validate_generated(construction.data(), construction.size(), spec->layout,
                                     has_battery ? &spec->battery_layout : nullptr);
    if (validation.result != AOAHID_OK) {
        set_error(validation.result, validation.field, validation.reason, 0, 0, 0, 0,
                  static_cast<std::uint32_t>(validation.offset), 0);
        return validation.result;
    }
    spec->kind = identity.kind;
    spec->android_status = identity.status;
    spec->descriptor = std::move(construction);
    spec->config = std::move(config);
    spec->report_capabilities.push_back(aoahid_report_capability{
        {spec->layout.has_report_id ? 1U : 0U, spec->layout.report_id, {0U, 0U, 0U}},
        static_cast<std::uint32_t>(spec->layout.wire_bytes)});
    if (has_battery) {
        spec->has_battery = true;
        spec->battery = battery.report;
        spec->report_capabilities.push_back(
            aoahid_report_capability{{1U, spec->battery_layout.report_id, {0U, 0U, 0U}},
                                     static_cast<std::uint32_t>(spec->battery_layout.wire_bytes)});
    }
    *out_spec = spec.release();
    aoa::detail::clear_error();
    return AOAHID_OK;
}

struct UsageSemanticFlags {
    bool accepted;
    bool relative;
    bool no_preferred;
};

inline UsageSemanticFlags usage_semantic_flags(const aoahid_usage_semantic semantic) noexcept {
    switch (semantic) {
    case AOAHID_USAGE_SELECTOR_BITMAP:
    case AOAHID_USAGE_MOMENTARY:
    case AOAHID_USAGE_RETRIGGER:
        return UsageSemanticFlags{true, false, false};
    case AOAHID_USAGE_ON_OFF_TOGGLE:
    case AOAHID_USAGE_ONE_SHOT:
        return UsageSemanticFlags{true, true, false};
    case AOAHID_USAGE_ON_OFF_MAINTAINED:
        return UsageSemanticFlags{true, false, true};
    case AOAHID_USAGE_ON_OFF_PAIR:
    case AOAHID_USAGE_LINEAR:
    case AOAHID_USAGE_DYNAMIC_VALUE:
    case AOAHID_USAGE_NAMED_ARRAY:
    default:
        return UsageSemanticFlags{false, false, false};
    }
}

inline bool controller_axis_usage(const aoahid_axis_role role, std::uint16_t* usage_page,
                                  std::uint16_t* usage) noexcept {
    if (usage_page == nullptr || usage == nullptr) {
        return false;
    }
    *usage_page = aoa::hid::usage::page_generic_desktop;
    switch (role) {
    case AOAHID_AXIS_X:
        *usage = aoa::hid::usage::x;
        return true;
    case AOAHID_AXIS_Y:
        *usage = aoa::hid::usage::y;
        return true;
    case AOAHID_AXIS_Z:
        *usage = aoa::hid::usage::z;
        return true;
    case AOAHID_AXIS_RX:
        *usage = aoa::hid::usage::rx;
        return true;
    case AOAHID_AXIS_RY:
        *usage = aoa::hid::usage::ry;
        return true;
    case AOAHID_AXIS_RZ:
        *usage = aoa::hid::usage::rz;
        return true;
    case AOAHID_AXIS_SLIDER:
        *usage = aoa::hid::usage::slider;
        return true;
    case AOAHID_AXIS_DIAL:
        *usage = aoa::hid::usage::dial;
        return true;
    case AOAHID_AXIS_WHEEL:
        *usage = aoa::hid::usage::wheel;
        return true;
    case AOAHID_AXIS_SIMULATION_RUDDER:
        *usage_page = aoa::hid::usage::page_simulation;
        *usage = aoa::hid::usage::simulation_rudder;
        return true;
    case AOAHID_AXIS_SIMULATION_THROTTLE:
        *usage_page = aoa::hid::usage::page_simulation;
        *usage = aoa::hid::usage::simulation_throttle;
        return true;
    case AOAHID_AXIS_SIMULATION_ACCELERATOR:
        *usage_page = aoa::hid::usage::page_simulation;
        *usage = aoa::hid::usage::simulation_accelerator;
        return true;
    case AOAHID_AXIS_SIMULATION_BRAKE:
        *usage_page = aoa::hid::usage::page_simulation;
        *usage = aoa::hid::usage::simulation_brake;
        return true;
    case AOAHID_AXIS_SIMULATION_STEERING:
        *usage_page = aoa::hid::usage::page_simulation;
        *usage = aoa::hid::usage::simulation_steering;
        return true;
    default:
        return false;
    }
}

inline bool controller_axis_domain_matches_usage(const aoahid_gamepad_axis_option& axis) noexcept {
    const auto& value = axis.value;
    switch (axis.role) {
    case AOAHID_AXIS_SIMULATION_ACCELERATOR:
    case AOAHID_AXIS_SIMULATION_BRAKE:
    case AOAHID_AXIS_SIMULATION_THROTTLE:
        return value.logical_minimum == 0 && value.logical_maximum > 0 && axis.neutral_value == 0;
    case AOAHID_AXIS_SIMULATION_STEERING:
    case AOAHID_AXIS_SIMULATION_RUDDER:
        return value.logical_minimum < 0 && value.logical_maximum > 0 && axis.neutral_value == 0;
    default:
        return true;
    }
}

template <typename Function>
aoahid_result abi_result(const char* field, Function&& function) noexcept {
    try {
        return function();
    } catch (const std::bad_alloc&) {
        set_error(AOAHID_ERR_INTERNAL, field,
                  "Memory allocation failed inside the C ABI boundary.");
        return AOAHID_ERR_INTERNAL;
    } catch (...) {
        set_error(AOAHID_ERR_INTERNAL, field,
                  "A C++ exception was contained at the C ABI boundary.");
        return AOAHID_ERR_INTERNAL;
    }
}

template <typename Function>
aoahid_result abi_spec_factory(aoahid_spec** out_spec, const char* field,
                               Function&& function) noexcept {
    aoa::detail::clear_error();
    if (out_spec == nullptr) {
        set_error(AOAHID_ERR_PARAM, "out_spec", "The output pointer is null.");
        return AOAHID_ERR_PARAM;
    }
    *out_spec = nullptr;
    return abi_result(field, std::forward<Function>(function));
}

template <typename Function> void abi_void(const char* field, Function&& function) noexcept {
    try {
        function();
    } catch (const std::bad_alloc&) {
        set_error(AOAHID_ERR_INTERNAL, field,
                  "Memory allocation failed inside the C ABI boundary.");
    } catch (...) {
        set_error(AOAHID_ERR_INTERNAL, field,
                  "A C++ exception was contained at the C ABI boundary.");
    }
}

} // namespace aoa::spec_build
