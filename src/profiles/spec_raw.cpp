// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * The raw profile (a report descriptor given by the caller): its Spec factory.
 */

#include "profiles/spec_support.hpp"

using namespace aoa::spec_build;

extern "C" {

static aoahid_result aoahid_spec_create_raw_impl(const aoahid_raw_options* options,
                                                 aoahid_spec** out_spec) {
    aoa::detail::clear_error();
    if (options == nullptr ||
        !aoa::detail::valid_struct(options, options == nullptr ? 0U : options->struct_size,
                                   static_cast<std::uint32_t>(sizeof(*options)), "raw_options")) {
        return AOAHID_ERR_PARAM;
    }
    if (out_spec == nullptr) {
        set_error(AOAHID_ERR_PARAM, "out_spec", "The output pointer is null.");
        return AOAHID_ERR_PARAM;
    }
    *out_spec = nullptr;
    if (options->reserved != 0U || options->acknowledges_no_android_support != 1U ||
        options->requires_output != 0U || options->requires_feature_response != 0U) {
        set_error(AOAHID_ERR_UNSUPPORTED, "raw.acknowledgements",
                  "Raw input requires explicit no-support acknowledgement and no Output or "
                  "Feature-response dependency.");
        return AOAHID_ERR_UNSUPPORTED;
    }
    // validate_raw permits exactly the 256 possible Report IDs. Bound every
    // count-derived allocation before using the caller's count so an invalid
    // raw table cannot turn a deterministic validation error into allocation
    // pressure or std::length_error.
    if (options->reports == nullptr || options->report_count == 0U) {
        set_error(AOAHID_ERR_UNSET_FIELD, "raw.reports",
                  "Raw descriptors require an explicit accepted-report table.");
        return AOAHID_ERR_UNSET_FIELD;
    }
    if (options->report_count > 256U) {
        set_error(AOAHID_ERR_OVERFLOW, "raw.reports",
                  "The accepted-report table exceeds the 256 possible Report IDs.");
        return AOAHID_ERR_OVERFLOW;
    }
    std::vector<std::uint8_t> trailing_valid_masks(options->report_count, std::uint8_t{0xFFU});
    aoa::hid::DescriptorRequirements requirements{};
    const aoa::hid::ValidationIssue validation =
        aoa::hid::validate_raw(options->descriptor, options->descriptor_length, options->reports,
                               options->report_count, trailing_valid_masks.data(), &requirements);
    if (validation.result != AOAHID_OK) {
        set_error(validation.result, validation.field, validation.reason, 0, 0, 0, 0,
                  static_cast<std::uint32_t>(validation.offset), 0);
        return validation.result;
    }
    auto spec = std::make_unique<aoahid_spec>();
    spec->kind = AOAHID_PROFILE_RAW;
    spec->android_status = AOAHID_ANDROID_UNKNOWN;
    spec->requirements = requirements;
    spec->descriptor.assign(options->descriptor, options->descriptor + options->descriptor_length);
    aoa::detail::RawConfig config{};
    config.reports.assign(options->reports, options->reports + options->report_count);
    config.trailing_valid_masks = std::move(trailing_valid_masks);
    for (const aoahid_raw_report& report : config.reports) {
        spec->report_capabilities.push_back(
            aoahid_report_capability{report.report_id, report.wire_length});
    }
    spec->config = std::move(config);
    *out_spec = spec.release();
    aoa::detail::clear_error();
    return AOAHID_OK;
}

aoahid_result AOAHID_CALL aoahid_spec_create_raw(const aoahid_raw_options* options,
                                                 aoahid_spec** out_spec) {
    return abi_spec_factory(out_spec, "spec.create_raw",
                            [&] { return aoahid_spec_create_raw_impl(options, out_spec); });
}

} // extern "C"
