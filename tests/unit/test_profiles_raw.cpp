// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Raw Specs: Report ID order in the descriptor, the accepted-report table rules, and
 * factory bounds checked before allocation. */
#include "unit/profile_support.hpp"

namespace profile_test {

void test_raw_report_id_order() {
    const std::array<std::uint8_t, 21> accepted{0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x85U,
                                                0x01U, 0x15U, 0x00U, 0x25U, 0x01U, 0x75U, 0x01U,
                                                0x95U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U};
    const aoahid_raw_report report{{1U, 1U, {}}, 2U};
    AOAHID_CHECK(aoa::hid::validate_raw(accepted.data(), accepted.size(), &report, 1U).result ==
                 AOAHID_OK);
    const std::array<std::uint8_t, 19> rejected{0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x15U,
                                                0x00U, 0x25U, 0x01U, 0x75U, 0x01U, 0x95U, 0x01U,
                                                0x81U, 0x02U, 0x85U, 0x01U, 0xC0U};
    AOAHID_CHECK(aoa::hid::validate_raw(rejected.data(), rejected.size(), &report, 1U).result !=
                 AOAHID_OK);

    const aoahid_raw_report no_id_report{{0U, 0U, {}}, 1U};
    const std::array<std::uint8_t, 17> output_item{0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U,
                                                   0x15U, 0x00U, 0x25U, 0x01U, 0x75U, 0x01U,
                                                   0x95U, 0x01U, 0x91U, 0x02U, 0xC0U};
    AOAHID_CHECK(
        aoa::hid::validate_raw(output_item.data(), output_item.size(), &no_id_report, 1U).result ==
        AOAHID_ERR_UNSUPPORTED);

    const std::array<std::uint8_t, 19> restored_id_zero{
        0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0xA4U, 0x85U, 0x01U, 0x75U,
        0x01U, 0x95U, 0x01U, 0x81U, 0x02U, 0xB4U, 0x81U, 0x02U, 0xC0U};
    AOAHID_CHECK(
        aoa::hid::validate_raw(restored_id_zero.data(), restored_id_zero.size(), &report, 1U)
            .result == AOAHID_ERR_PARAM);

    const std::array<std::uint8_t, 16> excessive_count{0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U,
                                                       0x75U, 0x01U, 0x97U, 0xFFU, 0xFFU, 0xFFU,
                                                       0xFFU, 0x81U, 0x02U, 0xC0U};
    AOAHID_CHECK(
        aoa::hid::validate_raw(excessive_count.data(), excessive_count.size(), &no_id_report, 1U)
            .result == AOAHID_ERR_PARAM);

    const std::array<std::uint8_t, 19> range_exceeds_size{
        0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x85U, 0x01U, 0x15U, 0x00U,
        0x25U, 0x03U, 0x75U, 0x01U, 0x95U, 0x01U, 0x81U, 0x02U, 0xC0U};
    AOAHID_CHECK(
        aoa::hid::validate_raw(range_exceeds_size.data(), range_exceeds_size.size(), &report, 1U)
            .result == AOAHID_ERR_PARAM);
}

void test_raw_report_table_report_id_rules() {
    const std::array<std::uint8_t, 21> descriptor{0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x85U,
                                                  0x01U, 0x15U, 0x00U, 0x25U, 0x01U, 0x75U, 0x01U,
                                                  0x95U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U};
    const auto result_for = [&](const aoahid_raw_report& report) {
        return aoa::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result;
    };
    AOAHID_CHECK(result_for({{1U, 1U, {}}, 2U}) == AOAHID_OK);
    // The flag is exactly zero or one.
    AOAHID_CHECK(result_for({{2U, 1U, {}}, 2U}) == AOAHID_ERR_PARAM);
    // A declared ID is 1 through 255, and a disabled ID is zero.
    AOAHID_CHECK(result_for({{1U, 0U, {}}, 2U}) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(result_for({{0U, 1U, {}}, 2U}) == AOAHID_ERR_PARAM);
    // The reserved bytes are zero.
    AOAHID_CHECK(result_for({{1U, 1U, {1U, 0U, 0U}}, 2U}) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(result_for({{1U, 1U, {0U, 0U, 1U}}, 2U}) == AOAHID_ERR_PARAM);
    // The wire length is not zero.
    AOAHID_CHECK(result_for({{1U, 1U, {}}, 0U}) == AOAHID_ERR_PARAM);

    // The same ID twice is one report identity listed twice.
    const std::array<aoahid_raw_report, 2> duplicate{aoahid_raw_report{{1U, 1U, {}}, 2U},
                                                     aoahid_raw_report{{1U, 1U, {}}, 2U}};
    AOAHID_CHECK(aoa::hid::validate_raw(descriptor.data(), descriptor.size(), duplicate.data(),
                                        duplicate.size())
                     .result == AOAHID_ERR_PARAM);

    // The raw spec reports the table back through the manifest unchanged.
    const aoahid_raw_report report{{1U, 1U, {}}, 2U};
    const aoahid_raw_options options{sizeof(aoahid_raw_options),
                                     0U,
                                     descriptor.data(),
                                     descriptor.size(),
                                     &report,
                                     1U,
                                     1U,
                                     0U,
                                     0U};
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_raw(&options, &spec) == AOAHID_OK);
    if (spec != nullptr) {
        aoahid_capability_manifest manifest{};
        manifest.struct_size = static_cast<std::uint32_t>(sizeof(manifest));
        AOAHID_CHECK(aoahid_spec_manifest(spec, &manifest) == AOAHID_OK);
        AOAHID_CHECK(manifest.report_count == 1U);
        if (manifest.report_count == 1U) {
            AOAHID_CHECK(manifest.reports[0].report_id.enabled == 1U);
            AOAHID_CHECK(manifest.reports[0].report_id.value == 1U);
            AOAHID_CHECK(manifest.reports[0].wire_length == 2U);
        }
        aoahid_spec_release(spec);
    }
}

void test_raw_factory_bounds_before_allocation() {
    const std::array<std::uint8_t, 1U> descriptor{0xC0U};
    const aoahid_raw_report report{};
    aoahid_raw_options options{};
    options.struct_size = static_cast<std::uint32_t>(sizeof(options));
    options.descriptor = descriptor.data();
    options.descriptor_length = descriptor.size();
    options.reports = &report;
    options.report_count = std::numeric_limits<std::size_t>::max();
    options.acknowledges_no_android_support = 1U;
    aoahid_spec* spec = reinterpret_cast<aoahid_spec*>(std::uintptr_t{1U});
    AOAHID_CHECK(aoahid_spec_create_raw(&options, &spec) == AOAHID_ERR_OVERFLOW);
    AOAHID_CHECK(spec == nullptr);

    options.reports = nullptr;
    AOAHID_CHECK(aoahid_spec_create_raw(&options, &spec) == AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(spec == nullptr);
}

} // namespace profile_test
