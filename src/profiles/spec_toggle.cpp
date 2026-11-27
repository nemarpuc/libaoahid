// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * The toggle profile (caller-chosen usage controls): its Spec factory.
 */

#include "profiles/spec_support.hpp"

using namespace aoa::spec_build;

namespace {

bool emit_usage_controls(DescriptorBuilder& builder, const aoahid_toggle_options& options) {
    if (!builder.begin_application(options.application_page, options.application_usage) ||
        !builder.set_report_id(options.report_id.enabled == 1U, options.report_id.value)) {
        return false;
    }
    for (std::size_t index = 0U; index < options.allowed_usage_count; ++index) {
        const UsageSemanticFlags flags = usage_semantic_flags(options.usage_semantics[index]);
        if (!flags.accepted ||
            !builder.variable(options.field_page, options.allowed_usages[index], 0, 1, 1U,
                              flags.relative, false, FieldSemantic::consumer_usage,
                              static_cast<std::uint16_t>(index), flags.no_preferred)) {
            return false;
        }
    }
    pad_report(builder);
    return builder.end_collection();
}

aoahid_result create_usage_control_spec(const aoahid_toggle_options* options,
                                        aoahid_spec** out_spec) {
    if (!aoa::detail::valid_struct(options, options == nullptr ? 0U : options->struct_size,
                                   static_cast<std::uint32_t>(sizeof(*options)),
                                   "toggle_options")) {
        return AOAHID_ERR_PARAM;
    }
    if (options->reserved != 0U || options->reserved16 != 0U ||
        !validate_report_id(options->report_id, "toggle.report_id")) {
        set_error(AOAHID_ERR_PARAM, "toggle.reserved",
                  "reserved fields must be zero and the Report ID declaration must be valid.");
        return AOAHID_ERR_PARAM;
    }
    const bool is_camera = options->field_page == aoa::hid::usage::page_camera_control;
    if (options->application_page == 0U || options->application_usage == 0U ||
        options->field_page == 0U) {
        set_error(AOAHID_ERR_UNSET_FIELD, "toggle.application",
                  "application_page, application_usage, and field_page are required.");
        return AOAHID_ERR_UNSET_FIELD;
    }
    if (options->allowed_usages == nullptr || options->allowed_usage_count == 0U) {
        set_error(AOAHID_ERR_UNSET_FIELD, "toggle.allowed_usages",
                  "At least one caller-selected Usage is required.");
        return AOAHID_ERR_UNSET_FIELD;
    }
    if (options->usage_semantics == nullptr) {
        set_error(AOAHID_ERR_UNSET_FIELD, "toggle.usage_semantics",
                  "Every allow-listed Usage needs an explicit HUT semantic.");
        return AOAHID_ERR_UNSET_FIELD;
    }
    if (options->expected_linux_event_types == nullptr ||
        options->expected_linux_codes == nullptr) {
        set_error(AOAHID_ERR_UNSET_FIELD, "toggle.expected_linux_events",
                  "Every allow-listed Usage needs explicit target-event evidence.");
        return AOAHID_ERR_UNSET_FIELD;
    }
    if (options->allowed_usage_count > std::numeric_limits<std::uint16_t>::max()) {
        set_error(AOAHID_ERR_OVERFLOW, "toggle.allowed_usage_count",
                  "The generated layout cannot index this many one-bit controls.");
        return AOAHID_ERR_OVERFLOW;
    }
    for (std::size_t index = 0; index < options->allowed_usage_count; ++index) {
        if (options->allowed_usages[index] == 0U ||
            options->expected_linux_event_types[index] == nullptr ||
            options->expected_linux_codes[index] == nullptr ||
            options->expected_linux_event_types[index][0] == '\0' ||
            options->expected_linux_codes[index][0] == '\0') {
            set_error(AOAHID_ERR_PARAM, "toggle.allowed_usages",
                      "Every nonzero Usage requires explicit expected Linux event type and code "
                      "evidence.");
            return AOAHID_ERR_PARAM;
        }
        if (std::find(options->allowed_usages, options->allowed_usages + index,
                      options->allowed_usages[index]) != options->allowed_usages + index) {
            set_error(AOAHID_ERR_PARAM, "toggle.allowed_usages",
                      "Each Usage may appear only once in the allow-list.");
            return AOAHID_ERR_PARAM;
        }
        if (options->application_page == options->field_page &&
            options->allowed_usages[index] == options->application_usage) {
            set_error(AOAHID_ERR_PARAM, "toggle.allowed_usages",
                      "The Application Collection Usage is not an Input control Usage.");
            return AOAHID_ERR_PARAM;
        }
        const aoahid_usage_semantic semantic = options->usage_semantics[index];
        if (!usage_semantic_flags(semantic).accepted) {
            if (semantic == AOAHID_USAGE_ON_OFF_PAIR || semantic == AOAHID_USAGE_LINEAR ||
                semantic == AOAHID_USAGE_DYNAMIC_VALUE || semantic == AOAHID_USAGE_NAMED_ARRAY) {
                set_error(AOAHID_ERR_UNSUPPORTED, "toggle.usage_semantics",
                          "This HUT semantic requires a value, direction, or collection API; "
                          "it cannot be represented by press/release.");
                return AOAHID_ERR_UNSUPPORTED;
            }
            set_error(AOAHID_ERR_PARAM, "toggle.usage_semantics",
                      "Every Usage must select one defined aoahid_usage_semantic value.");
            return AOAHID_ERR_PARAM;
        }
        if (is_camera && options->allowed_usages[index] != aoa::hid::usage::camera_auto_focus &&
            options->allowed_usages[index] != aoa::hid::usage::camera_shutter) {
            set_error(AOAHID_ERR_PARAM, "toggle.allowed_usages",
                      "HUT 1.7 Camera Control defines Auto-focus 0x20 and Shutter "
                      "0x21; no other Usage exists on that page.");
            return AOAHID_ERR_PARAM;
        }
        if (is_camera && semantic != AOAHID_USAGE_ONE_SHOT) {
            set_error(AOAHID_ERR_UNSUPPORTED, "toggle.usage_semantics",
                      "HUT 1.7 Table 35.1 defines Auto-focus and Shutter as One Shot Controls.");
            return AOAHID_ERR_UNSUPPORTED;
        }
    }
    aoa::detail::ConsumerConfig config{};
    config.options = *options;
    config.allowed_usages.assign(options->allowed_usages,
                                 options->allowed_usages + options->allowed_usage_count);
    config.usage_semantics.assign(options->usage_semantics,
                                  options->usage_semantics + options->allowed_usage_count);
    for (std::size_t index = 0U; index < options->allowed_usage_count; ++index) {
        config.expected_linux_event_types.emplace_back(options->expected_linux_event_types[index]);
        config.expected_linux_codes.emplace_back(options->expected_linux_codes[index]);
    }
    config.options.allowed_usages = nullptr;
    config.options.allowed_usage_count = config.allowed_usages.size();
    config.options.usage_semantics = nullptr;
    config.options.expected_linux_event_types = nullptr;
    config.options.expected_linux_codes = nullptr;
    const BatteryRequest battery{options->enable_battery, options->battery, options->report_id};
    if (!validate_battery(battery, kToggleBatteryNames)) {
        return AOAHID_ERR_PARAM;
    }
    return build_generated_spec(
        GeneratedSpecIdentity{AOAHID_PROFILE_TOGGLE, AOAHID_ANDROID_CONDITIONAL}, std::move(config),
        battery, [=](DescriptorBuilder& builder) { return emit_usage_controls(builder, *options); },
        out_spec);
}

} // namespace

extern "C" {

static aoahid_result aoahid_spec_create_toggle_impl(const aoahid_toggle_options* options,
                                                    aoahid_spec** out_spec) {
    aoa::detail::clear_error();
    return create_usage_control_spec(options, out_spec);
}

aoahid_result AOAHID_CALL aoahid_spec_create_toggle(const aoahid_toggle_options* options,
                                                    aoahid_spec** out_spec) {
    return abi_spec_factory(out_spec, "spec.create_toggle",
                            [&] { return aoahid_spec_create_toggle_impl(options, out_spec); });
}

} // extern "C"
