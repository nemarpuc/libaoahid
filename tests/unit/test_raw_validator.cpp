// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* The raw-descriptor validator: what the library takes from a descriptor (the wire length of each
 * report, and what the target's parser is asked to hold), Local state and delimiters, arithmetic
 * limits and failure paths, the structure it leaves to the target, and a table of descriptors and
 * report tables with the result and field each one is given. No transport is involved. */
#include "hid/validator.hpp"
#include "test.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

namespace {

void append_u32_item(std::vector<std::uint8_t>* descriptor, const std::uint8_t prefix,
                     const std::uint32_t value) {
    descriptor->push_back(prefix);
    descriptor->push_back(static_cast<std::uint8_t>(value & 0xFFU));
    descriptor->push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    descriptor->push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    descriptor->push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

std::vector<std::uint8_t> application_preamble() {
    // Usage Page (Generic Desktop), Usage (Keyboard), Application Collection.
    return {0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U};
}

void append_globals(std::vector<std::uint8_t>* descriptor, const std::uint8_t report_size,
                    const std::uint32_t report_count) {
    // Logical Minimum 0, Logical Maximum 1, Report Size, Report Count.
    descriptor->insert(descriptor->end(), {0x15U, 0x00U, 0x25U, 0x01U, 0x75U, report_size});
    append_u32_item(descriptor, 0x97U, report_count);
}

aoahid_raw_report idless_report(const std::uint32_t wire_length) {
    aoahid_raw_report report{};
    report.wire_length = wire_length;
    return report;
}

void test_target_neutral_raw_requirements() {
    // The state transitions under test are from the exact target source:
    // https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-core.c
    // hid_parser_global, hid_parser_local, hid_add_field, hid_register_field.
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        // Usage Minimum 0 through Usage Maximum 20000 is 20001 Local Usages.
        descriptor.insert(descriptor.end(),
                          {0x1AU, 0x00U, 0x00U, 0x2AU, 0x20U, 0x4EU, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        aoa::hid::DescriptorRequirements requirements{};
        const aoa::hid::ValidationIssue result = aoa::hid::validate_raw(
            descriptor.data(), descriptor.size(), &report, 1U, nullptr, &requirements);
        AOAHID_CHECK(result.result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_fields_per_report == 1U);
        AOAHID_CHECK(requirements.maximum_global_stack_depth == 0U);
        AOAHID_CHECK(requirements.maximum_report_size_bits == 1U);
        AOAHID_CHECK(requirements.maximum_usages == 20001U);
        AOAHID_CHECK(requirements.maximum_report_data_bits == 1U);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        // A nearly full 32-bit Usage range requires a 64-bit count and must
        // not be expanded into individual entries by validation.
        descriptor.insert(descriptor.end(), {0x1BU, 0x00U, 0x00U, 0x00U, 0x00U, 0x2BU, 0xFEU, 0xFFU,
                                             0xFFU, 0xFFU, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        aoa::hid::DescriptorRequirements requirements{};
        const aoa::hid::ValidationIssue result = aoa::hid::validate_raw(
            descriptor.data(), descriptor.size(), &report, 1U, nullptr, &requirements);
        AOAHID_CHECK(result.result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_fields_per_report == 1U);
        AOAHID_CHECK(requirements.maximum_global_stack_depth == 0U);
        AOAHID_CHECK(requirements.maximum_report_size_bits == 1U);
        AOAHID_CHECK(requirements.maximum_usages ==
                     static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
        AOAHID_CHECK(requirements.maximum_report_data_bits == 1U);
    }
    {
        auto descriptor = application_preamble();
        // 524280 one-bit values occupy exactly 65535 bytes. Validation must
        // use checked arithmetic, not a Report Count-sized loop.
        append_globals(&descriptor, 1U, 524280U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(65535U);
        aoa::hid::DescriptorRequirements requirements{};
        const aoa::hid::ValidationIssue result = aoa::hid::validate_raw(
            descriptor.data(), descriptor.size(), &report, 1U, nullptr, &requirements);
        AOAHID_CHECK(result.result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_fields_per_report == 1U);
        AOAHID_CHECK(requirements.maximum_global_stack_depth == 0U);
        AOAHID_CHECK(requirements.maximum_report_size_bits == 1U);
        AOAHID_CHECK(requirements.maximum_usages == 524280U);
        AOAHID_CHECK(requirements.maximum_report_data_bits == 524280U);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        for (std::uint32_t field = 0U; field < 257U; ++field) {
            descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U});
        }
        descriptor.push_back(0xC0U);
        const aoahid_raw_report report = idless_report(33U);
        std::uint8_t trailing_mask = 0U;
        aoa::hid::DescriptorRequirements requirements{};
        const aoa::hid::ValidationIssue result = aoa::hid::validate_raw(
            descriptor.data(), descriptor.size(), &report, 1U, &trailing_mask, &requirements);
        AOAHID_CHECK(result.result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_fields_per_report == 257U);
        AOAHID_CHECK(requirements.maximum_global_stack_depth == 0U);
        AOAHID_CHECK(requirements.maximum_report_size_bits == 1U);
        AOAHID_CHECK(requirements.maximum_usages == 1U);
        AOAHID_CHECK(requirements.maximum_report_data_bits == 257U);
        AOAHID_CHECK(trailing_mask == 0x01U);
    }
    {
        auto descriptor = application_preamble();
        descriptor.insert(descriptor.end(), {0x85U, 0x01U});
        append_globals(&descriptor, 1U, 1U);
        for (std::uint32_t field = 0U; field < 2U; ++field) {
            descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U});
        }
        descriptor.insert(descriptor.end(), {0x85U, 0x02U});
        for (std::uint32_t field = 0U; field < 3U; ++field) {
            descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U});
        }
        descriptor.push_back(0xC0U);
        const std::array<aoahid_raw_report, 2U> reports{aoahid_raw_report{{1U, 1U, {}}, 2U},
                                                        aoahid_raw_report{{1U, 2U, {}}, 2U}};
        aoa::hid::DescriptorRequirements requirements{};
        const aoa::hid::ValidationIssue result =
            aoa::hid::validate_raw(descriptor.data(), descriptor.size(), reports.data(),
                                   reports.size(), nullptr, &requirements);
        AOAHID_CHECK(result.result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_fields_per_report == 3U);
        AOAHID_CHECK(requirements.maximum_global_stack_depth == 0U);
        AOAHID_CHECK(requirements.maximum_report_size_bits == 1U);
        AOAHID_CHECK(requirements.maximum_usages == 1U);
        AOAHID_CHECK(requirements.maximum_report_data_bits == 3U);
    }
    {
        auto descriptor = application_preamble();
        // Global Push/Pop are zero-byte short items. Five balanced levels are
        // valid descriptor state for a target-neutral Spec, but exceed the
        // selected target revision's HID_GLOBAL_STACK_SIZE policy of four.
        descriptor.insert(descriptor.end(),
                          {0xA4U, 0xA4U, 0xA4U, 0xA4U, 0xA4U, 0xB4U, 0xB4U, 0xB4U, 0xB4U, 0xB4U});
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        aoa::hid::DescriptorRequirements requirements{};
        const aoa::hid::ValidationIssue result = aoa::hid::validate_raw(
            descriptor.data(), descriptor.size(), &report, 1U, nullptr, &requirements);
        AOAHID_CHECK(result.result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_fields_per_report == 1U);
        AOAHID_CHECK(requirements.maximum_global_stack_depth == 5U);
        AOAHID_CHECK(requirements.maximum_report_size_bits == 1U);
        AOAHID_CHECK(requirements.maximum_usages == 1U);
        AOAHID_CHECK(requirements.maximum_report_data_bits == 1U);
    }
    {
        auto descriptor = application_preamble();
        // The exact target rejects a Report Size declaration above 256 even
        // if a later Global item overwrites it before Input. A target-neutral
        // Spec records the declaration so Node registration can apply policy.
        descriptor.insert(descriptor.end(), {0x77U, 0x01U, 0x01U, 0x00U, 0x00U});
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        aoa::hid::DescriptorRequirements requirements{};
        const aoa::hid::ValidationIssue result = aoa::hid::validate_raw(
            descriptor.data(), descriptor.size(), &report, 1U, nullptr, &requirements);
        AOAHID_CHECK(result.result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_report_size_bits == 257U);
        AOAHID_CHECK(requirements.maximum_report_data_bits == 1U);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        // Local Usage items are parsed even after the final Collection. The
        // exact target rejects the 12,289th entry; requirements must not only
        // sample Local state when a Main item appears.
        descriptor.insert(descriptor.end(), 12289U, 0x08U);
        const aoahid_raw_report report = idless_report(1U);
        aoa::hid::DescriptorRequirements requirements{};
        const aoa::hid::ValidationIssue result = aoa::hid::validate_raw(
            descriptor.data(), descriptor.size(), &report, 1U, nullptr, &requirements);
        AOAHID_CHECK(result.result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_usages == 12289U);
    }
}

void test_local_state_and_delimiters() {
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        // HID 1.11 section 6.2.2.8 does not state that Usage Maximum without
        // Usage Minimum is invalid or define a default minimum. The exact
        // target zero-initializes local state, so a lone Maximum of 3 expands
        // the raw range 0 through 3. Preserve that implementation observation
        // instead of inventing a stricter conformance rule.
        descriptor.insert(descriptor.end(), {0x29U, 0x03U, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        aoa::hid::DescriptorRequirements requirements{};
        const aoa::hid::ValidationIssue result = aoa::hid::validate_raw(
            descriptor.data(), descriptor.size(), &report, 1U, nullptr, &requirements);
        AOAHID_CHECK(result.result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_usages == 4U);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(),
                          {// Branch zero Usage.
                           0x09U, 0x01U,
                           // First delimiter branch: five more Usages.
                           0xA9U, 0x01U, 0x19U, 0x0AU, 0x29U, 0x0EU, 0xA9U, 0x00U,
                           // Second branch is ignored by the audited parser.
                           0xA9U, 0x01U, 0x09U, 0x7FU, 0xA9U, 0x00U, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        aoa::hid::DescriptorRequirements requirements{};
        const aoa::hid::ValidationIssue result = aoa::hid::validate_raw(
            descriptor.data(), descriptor.size(), &report, 1U, nullptr, &requirements);
        AOAHID_CHECK(result.result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_fields_per_report == 1U);
        AOAHID_CHECK(requirements.maximum_usages == 6U);
    }
    {
        // The five-Usage range belongs to the Collection Main item and is
        // reset afterwards. The following constant Input is padding, not a
        // registered field.
        std::vector<std::uint8_t> descriptor{0x05U, 0x01U, 0x19U, 0x01U,
                                             0x29U, 0x05U, 0xA1U, 0x01U};
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x81U, 0x01U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        aoa::hid::DescriptorRequirements requirements{};
        const aoa::hid::ValidationIssue result = aoa::hid::validate_raw(
            descriptor.data(), descriptor.size(), &report, 1U, nullptr, &requirements);
        AOAHID_CHECK(result.result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_fields_per_report == 0U);
        AOAHID_CHECK(requirements.maximum_usages == 5U);
    }
    {
        // Delimiters that nest, close without opening or stay open do not change what a report
        // holds; whether the target parser takes them is its answer. No Input item, so no report.
        for (const std::vector<std::uint8_t>& suffix :
             {std::vector<std::uint8_t>{0xA9U, 0x01U, 0xA9U, 0x01U},
              std::vector<std::uint8_t>{0xA9U, 0x00U}, std::vector<std::uint8_t>{0xA9U, 0x01U}}) {
            auto descriptor = application_preamble();
            descriptor.insert(descriptor.end(), suffix.begin(), suffix.end());
            descriptor.push_back(0xC0U);
            AOAHID_CHECK(
                aoa::hid::validate_raw(descriptor.data(), descriptor.size(), nullptr, 0U).result ==
                AOAHID_OK);
        }
    }
    {
        // A Usage range that ends below its start counts nothing.
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x19U, 0x05U, 0x29U, 0x04U, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        aoa::hid::DescriptorRequirements requirements{};
        AOAHID_CHECK(aoa::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U,
                                            nullptr, &requirements)
                         .result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_usages == 1U);
        AOAHID_CHECK(requirements.maximum_fields_per_report == 0U);
    }
    {
        // Delimiters around the Usages of an Application Collection, of an Array item, or left
        // open at a Main item are not the library's to refuse.
        const std::vector<std::uint8_t> application{0x05U, 0x01U, 0xA9U, 0x01U, 0x09U, 0x06U,
                                                    0xA9U, 0x00U, 0xA1U, 0x01U, 0xC0U};
        AOAHID_CHECK(
            aoa::hid::validate_raw(application.data(), application.size(), nullptr, 0U).result ==
            AOAHID_OK);
        const aoahid_raw_report report = idless_report(1U);
        auto array = application_preamble();
        append_globals(&array, 8U, 1U);
        array.insert(array.end(), {0xA9U, 0x01U, 0x09U, 0x04U, 0xA9U, 0x00U, 0x81U, 0x00U, 0xC0U});
        AOAHID_CHECK(aoa::hid::validate_raw(array.data(), array.size(), &report, 1U).result ==
                     AOAHID_OK);
        auto open = application_preamble();
        append_globals(&open, 1U, 1U);
        open.insert(open.end(), {0xA9U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        AOAHID_CHECK(aoa::hid::validate_raw(open.data(), open.size(), &report, 1U).result ==
                     AOAHID_OK);
    }
}

// The wire length of a report is all the library takes from a data item: Report Size times Report
// Count, summed per Report ID. A range, a width or a flag the target parser would object to is
// the target's answer, given when the device is created.
void test_raw_arithmetic_and_failure_paths() {
    {
        // With a nonnegative Logical Minimum the Maximum item is unsigned (HID 1.11 section
        // 6.2.2.7), and none of the extents changes the length.
        auto descriptor = application_preamble();
        descriptor.insert(descriptor.end(),
                          {0x15U, 0x00U, 0x27U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x75U, 0x20U, 0x95U,
                           0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(4U);
        AOAHID_CHECK(
            aoa::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            AOAHID_OK);
        descriptor[14U] = 0x1FU; // Report Size 31 cannot hold 0xffffffff; the length is the same.
        AOAHID_CHECK(
            aoa::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            AOAHID_OK);
        // A Logical Maximum below the Minimum is likewise left to the target.
        descriptor[9U] = 0x00U;
        descriptor[10U] = 0x00U;
        descriptor[11U] = 0x00U;
        descriptor[12U] = 0x80U;
        AOAHID_CHECK(
            aoa::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            AOAHID_OK);
    }
    {
        // A field wider than 32 bits, or one that spans five bytes, only adds bits.
        auto wide = application_preamble();
        append_globals(&wide, 33U, 1U);
        wide.insert(wide.end(), {0x09U, 0x01U, 0x81U, 0x01U, 0xC0U});
        const aoahid_raw_report five = idless_report(5U);
        aoa::hid::DescriptorRequirements requirements{};
        AOAHID_CHECK(
            aoa::hid::validate_raw(wide.data(), wide.size(), &five, 1U, nullptr, &requirements)
                .result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_report_size_bits == 33U);
        AOAHID_CHECK(requirements.maximum_report_data_bits == 33U);

        auto span = application_preamble();
        append_globals(&span, 31U, 2U);
        span.insert(span.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report eight = idless_report(8U);
        AOAHID_CHECK(aoa::hid::validate_raw(span.data(), span.size(), &eight, 1U).result ==
                     AOAHID_OK);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 32U, std::numeric_limits<std::uint32_t>::max());
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        // On a 64-bit host the computed wire length exceeds the public u32
        // table field; on a 32-bit host the Size*Count product overflows
        // size_t first. Both paths are an arithmetic overflow, not a generic
        // table mismatch.
        AOAHID_CHECK(
            aoa::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            AOAHID_ERR_OVERFLOW);
    }
    {
        // The raw profile is Input-only: an Output item is refused.
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x91U, 0x02U, 0xC0U});
        const aoa::hid::ValidationIssue result =
            aoa::hid::validate_raw(descriptor.data(), descriptor.size(), nullptr, 0U);
        AOAHID_CHECK(result.result == AOAHID_ERR_UNSUPPORTED);
    }
    {
        // A long item (HID 1.11 section 6.2.2.3) is stepped over; one that is cut off is refused
        // because its end is not known.
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        auto with_long = descriptor;
        with_long.insert(with_long.end(), {0xFEU, 0x02U, 0x10U, 0xAAU, 0xBBU});
        AOAHID_CHECK(
            aoa::hid::validate_raw(with_long.data(), with_long.size(), &report, 1U).result ==
            AOAHID_OK);
        auto cut = descriptor;
        cut.insert(cut.end(), {0xFEU, 0x05U, 0x10U, 0xAAU});
        AOAHID_CHECK(aoa::hid::validate_raw(cut.data(), cut.size(), &report, 1U).result ==
                     AOAHID_ERR_PARAM);
        auto header_cut = descriptor;
        header_cut.insert(header_cut.end(), {0xFEU, 0x00U});
        AOAHID_CHECK(
            aoa::hid::validate_raw(header_cut.data(), header_cut.size(), &report, 1U).result ==
            AOAHID_ERR_PARAM);
    }
    {
        // Items the parser has no use for are passed over: a reserved Global tag, Physical and Unit
        // items with or without their companions, a transient Unit, a Unit Exponent of any size.
        for (const std::vector<std::uint8_t>& extra :
             {std::vector<std::uint8_t>{0xC5U, 0x00U},
              std::vector<std::uint8_t>{0x35U, 0x00U, 0x45U, 0x01U, 0x55U, 0x0CU, 0x65U, 0x11U},
              std::vector<std::uint8_t>{0x65U, 0x11U, 0x65U, 0x00U},
              std::vector<std::uint8_t>{0x65U, 0x11U}, std::vector<std::uint8_t>{0x65U, 0x05U},
              std::vector<std::uint8_t>{0x67U, 0x11U, 0x00U, 0x00U, 0x10U},
              std::vector<std::uint8_t>{0x55U, 0xFCU},
              std::vector<std::uint8_t>{0x07U, 0x00U, 0x00U, 0x01U, 0x00U},
              std::vector<std::uint8_t>{0x15U, 0x02U, 0x25U, 0x01U}}) {
            auto descriptor = application_preamble();
            append_globals(&descriptor, 1U, 1U);
            descriptor.insert(descriptor.end(), extra.begin(), extra.end());
            descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
            const aoahid_raw_report report = idless_report(1U);
            AOAHID_CHECK(
                aoa::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
                AOAHID_OK);
        }
    }
    {
        // Pushes and Pops that do not pair up leave the state as it is.
        auto descriptor = application_preamble();
        descriptor.insert(descriptor.end(), {0xB4U, 0xA4U});
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        AOAHID_CHECK(
            aoa::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            AOAHID_OK);
    }
}

// Whether a collection has a Usage, whether control data carries its Usage Page, Usage and
// Logical extents, how the Collections nest, what the flag bits of a Main item are: the library
// does not check any of it.
void test_structure_the_target_decides() {
    const aoahid_raw_report report = idless_report(1U);
    const auto check_accepted = [&report](const std::vector<std::uint8_t>& descriptor,
                                          const bool has_input) {
        AOAHID_CHECK(aoa::hid::validate_raw(descriptor.data(), descriptor.size(),
                                            has_input ? &report : nullptr, has_input ? 1U : 0U)
                         .result == AOAHID_OK);
    };

    check_accepted({0x05U, 0x01U, 0xA1U, 0x01U, 0xC0U}, false); // a Collection without a Usage
    check_accepted({0x05U, 0x01U, 0x09U, 0x06U}, false);        // no Collection at all
    check_accepted({0xC0U, 0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0xC0U}, false);
    check_accepted({0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U}, false);        // never closed
    check_accepted({0x05U, 0x01U, 0x09U, 0x01U, 0xA1U, 0x00U, 0xC0U}, false); // top level Physical

    // Control data without its Usage, Logical Minimum, Logical Maximum or Usage Page: the target
    // takes a data item with no Usage as padding.
    for (const std::vector<std::uint8_t>& globals :
         {std::vector<std::uint8_t>{0x15U, 0x00U, 0x25U, 0x01U, 0x75U, 0x01U, 0x95U, 0x01U},
          std::vector<std::uint8_t>{0x09U, 0x01U, 0x25U, 0x01U, 0x75U, 0x01U, 0x95U, 0x01U},
          std::vector<std::uint8_t>{0x09U, 0x01U, 0x15U, 0x00U, 0x75U, 0x01U, 0x95U, 0x01U}}) {
        auto descriptor = application_preamble();
        descriptor.insert(descriptor.end(), globals.begin(), globals.end());
        descriptor.insert(descriptor.end(), {0x81U, 0x02U, 0xC0U});
        check_accepted(descriptor, true);
    }
    check_accepted({0x0BU, 0x06U, 0x00U, 0x01U, 0x00U, 0xA1U, 0x01U, 0x0BU,
                    0x01U, 0x00U, 0x01U, 0x00U, 0x15U, 0x00U, 0x25U, 0x01U,
                    0x75U, 0x01U, 0x95U, 0x01U, 0x81U, 0x02U, 0xC0U},
                   true);

    // A reserved Main tag or item type is passed over, and so is a reserved Local tag.
    for (const std::vector<std::uint8_t>& extra :
         {std::vector<std::uint8_t>{0xD0U}, std::vector<std::uint8_t>{0x0DU, 0x00U},
          std::vector<std::uint8_t>{0x69U, 0x00U}, std::vector<std::uint8_t>{0x0CU}}) {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U});
        descriptor.insert(descriptor.end(), extra.begin(), extra.end());
        descriptor.insert(descriptor.end(), {0x81U, 0x02U, 0xC0U});
        check_accepted(descriptor, true);
    }

    // Reserved Collection types, reserved flag bits, Buffered Bytes off a byte boundary and
    // delimiters on an Input item.
    {
        auto descriptor = application_preamble();
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0xA1U, 0x07U});
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U, 0xC0U});
        check_accepted(descriptor, true);
    }
    for (const std::vector<std::uint8_t>& input :
         {std::vector<std::uint8_t>{0x82U, 0x80U, 0x00U},
          std::vector<std::uint8_t>{0x82U, 0x00U, 0x02U}}) {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U});
        descriptor.insert(descriptor.end(), input.begin(), input.end());
        descriptor.push_back(0xC0U);
        check_accepted(descriptor, true);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0x75U, 0x08U, 0x95U, 0x01U,
                                             0x09U, 0x02U, 0x82U, 0x00U, 0x01U, 0xC0U});
        const aoahid_raw_report two = idless_report(2U);
        AOAHID_CHECK(
            aoa::hid::validate_raw(descriptor.data(), descriptor.size(), &two, 1U).result ==
            AOAHID_OK);
    }
    for (const std::vector<std::uint8_t>& delimited :
         {std::vector<std::uint8_t>{0xA9U, 0x02U, 0x09U, 0x01U, 0xA9U, 0x00U, 0x81U, 0x02U},
          std::vector<std::uint8_t>{0xA9U, 0x01U, 0x09U, 0x01U, 0xA9U, 0x00U, 0x81U, 0x01U}}) {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), delimited.begin(), delimited.end());
        descriptor.push_back(0xC0U);
        check_accepted(descriptor, true);
    }
    // A Usage Minimum that is extended while the Maximum is short.
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(),
                          {0x1BU, 0x01U, 0x00U, 0x01U, 0x00U, 0x29U, 0x02U, 0x81U, 0x02U, 0xC0U});
        check_accepted(descriptor, true);
    }
}

// How Usage ranges are counted, and the Local items the library ignores.
void test_usage_ranges_and_local_items() {
    AOAHID_CHECK(aoa::hid::unit_is_valid(0U));
    AOAHID_CHECK(aoa::hid::unit_is_valid(0x11U));
    AOAHID_CHECK(aoa::hid::unit_is_valid(0x14U));
    AOAHID_CHECK(aoa::hid::unit_is_valid(0x0FU));
    AOAHID_CHECK(!aoa::hid::unit_is_valid(0x05U));
    AOAHID_CHECK(!aoa::hid::unit_is_valid(0x10000011U));
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        // The specified target expands a short Usage range from the raw Local
        // item values, then hid_add_usage() concatenates the Usage Page. The
        // loop is therefore fffe..ffff (two entries), not a 32-bit wrap.
        descriptor.insert(descriptor.end(), {0x06U, 0xFFU, 0xFFU, 0x1AU, 0xFEU, 0xFFU, 0x2AU, 0xFFU,
                                             0xFFU, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        aoa::hid::DescriptorRequirements requirements{};
        AOAHID_CHECK(aoa::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U,
                                            nullptr, &requirements)
                         .result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_usages == 2U);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        // HID 1.11 gives the short Minimum page 1 and the extended Maximum an
        // embedded page 1. The specified target nevertheless expands its raw
        // item_udata range 1..00010002 before adding pages. Record that exact
        // parser requirement so its 12,288-Usage policy can reject it before
        // registration instead of understating it as two entries.
        descriptor.insert(descriptor.end(),
                          {0x19U, 0x01U, 0x2BU, 0x02U, 0x00U, 0x01U, 0x00U, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        aoa::hid::DescriptorRequirements requirements{};
        AOAHID_CHECK(aoa::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U,
                                            nullptr, &requirements)
                         .result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_usages == 65538U);
    }
    {
        // A Usage Maximum of UINT32_MAX is counted like any other.
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x1BU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x2BU, 0xFFU, 0xFFU,
                                             0xFFU, 0xFFU, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        aoa::hid::DescriptorRequirements requirements{};
        AOAHID_CHECK(aoa::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U,
                                            nullptr, &requirements)
                         .result == AOAHID_OK);
        AOAHID_CHECK(requirements.maximum_usages == 1U);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(),
                          {0x15U, 0x00U, 0x25U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0x75U, 0x08U,
                           0x95U, 0x01U, 0x09U, 0x02U, 0x82U, 0x00U, 0x01U, 0xC0U});
        const aoahid_raw_report report = idless_report(2U);
        AOAHID_CHECK(
            aoa::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            AOAHID_OK);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 8U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x82U, 0x00U, 0x01U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        AOAHID_CHECK(
            aoa::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            AOAHID_OK);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        // Designator Index (tag 3) and String Index (tag 7) are defined Local
        // items. The selected target ignores both, which does not change the
        // Input report layout.
        descriptor.insert(descriptor.end(),
                          {0x39U, 0x01U, 0x79U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        const aoahid_raw_report report = idless_report(1U);
        AOAHID_CHECK(
            aoa::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            AOAHID_OK);
    }
}

struct Case {
    const char* name;
    std::vector<std::uint8_t> descriptor;
    std::vector<aoahid_raw_report> reports;
    aoahid_result result;
    const char* field;
};

constexpr aoahid_raw_report no_id(const std::uint32_t length) { return {{0U, 0U, {}}, length}; }
constexpr aoahid_raw_report with_id(const std::uint8_t id, const std::uint32_t length) {
    return {{1U, id, {}}, length};
}

// Usage Page (Generic Desktop), Usage (Keyboard), Collection (Application).
std::vector<std::uint8_t> open_application() { return {0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U}; }

// Logical 0..1, Report Size 1, Report Count 8, Usage, Input (Data, Variable).
std::vector<std::uint8_t> eight_bits() {
    return {0x15U, 0x00U, 0x25U, 0x01U, 0x75U, 0x01U, 0x95U, 0x08U, 0x09U, 0x01U, 0x81U, 0x02U};
}

std::vector<std::uint8_t> join(std::initializer_list<std::vector<std::uint8_t>> parts) {
    std::vector<std::uint8_t> out;
    for (const auto& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

const std::vector<std::uint8_t> kEnd{0xC0U};

std::vector<Case> cases() {
    const auto valid = join({open_application(), eight_bits(), kEnd});
    const auto two_ids = join(
        {open_application(), {0x85U, 0x01U}, eight_bits(), {0x85U, 0x02U}, eight_bits(), kEnd});
    return {
        {"valid", valid, {no_id(1U)}, AOAHID_OK, nullptr},
        {"empty table for a descriptor with Input", valid, {}, AOAHID_ERR_PARAM, "raw.reports"},
        {"empty table, no Input report",
         join({open_application(), {0x15U, 0x00U, 0x25U, 0x01U, 0x75U, 0x01U, 0x95U, 0x08U}, kEnd}),
         {},
         AOAHID_OK,
         nullptr},
        {"empty table, zero-bit Input",
         join({open_application(), {0x75U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U}, kEnd}),
         {},
         AOAHID_OK,
         nullptr},
        {"wrong length", valid, {no_id(2U)}, AOAHID_ERR_PARAM, "raw.reports"},
        {"table lists an ID the descriptor lacks",
         valid,
         {no_id(1U), with_id(1U, 2U)},
         AOAHID_ERR_PARAM,
         "raw.reports"},
        {"table omits a report", two_ids, {with_id(1U, 2U)}, AOAHID_ERR_PARAM, "raw.reports"},
        {"table with IDs, descriptor without",
         valid,
         {with_id(1U, 2U)},
         AOAHID_ERR_PARAM,
         "raw.reports"},
        {"both IDs listed", two_ids, {with_id(1U, 2U), with_id(2U, 2U)}, AOAHID_OK, nullptr},
        {"usage page wider than 16 bits",
         join({{0x07U, 0x00U, 0x00U, 0x01U, 0x00U},
               {0x09U, 0x06U, 0xA1U, 0x01U},
               eight_bits(),
               kEnd}),
         {no_id(1U)},
         AOAHID_OK,
         nullptr},
        {"report ID zero",
         join({open_application(), {0x85U, 0x00U}, eight_bits(), kEnd}),
         {no_id(1U)},
         AOAHID_ERR_PARAM,
         "raw.report_id"},
        {"report ID wider than 255",
         join({open_application(), {0x86U, 0x00U, 0x01U}, eight_bits(), kEnd}),
         {no_id(1U)},
         AOAHID_ERR_PARAM,
         "raw.report_id"},
        {"report ID after data",
         join({open_application(), eight_bits(), {0x85U, 0x01U}, kEnd}),
         {no_id(1U)},
         AOAHID_ERR_PARAM,
         "raw.report_id"},
        {"pop without push",
         join({open_application(), {0xB4U}, eight_bits(), kEnd}),
         {no_id(1U)},
         AOAHID_OK,
         nullptr},
        {"push without pop",
         join({open_application(), {0xA4U}, eight_bits(), kEnd}),
         {no_id(1U)},
         AOAHID_OK,
         nullptr},
        {"end collection first",
         join({kEnd, open_application(), eight_bits(), kEnd}),
         {no_id(1U)},
         AOAHID_OK,
         nullptr},
        {"collection never closed",
         join({open_application(), eight_bits()}),
         {no_id(1U)},
         AOAHID_OK,
         nullptr},
        {"top level is a Physical collection",
         join({{0x05U, 0x01U, 0x09U, 0x01U, 0xA1U, 0x00U}, eight_bits(), kEnd}),
         {no_id(1U)},
         AOAHID_OK,
         nullptr},
        {"no collection at all", {0x05U, 0x01U, 0x09U, 0x06U}, {}, AOAHID_OK, nullptr},
        {"input outside a collection",
         join({eight_bits(), open_application(), kEnd}),
         {no_id(1U)},
         AOAHID_OK,
         nullptr},
        {"input without a report count",
         join({open_application(), {0x75U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U}, kEnd}),
         {no_id(1U)},
         AOAHID_ERR_PARAM,
         "raw.reports"},
        {"report size over 32 bits",
         join({open_application(), {0x75U, 0x21U, 0x95U, 0x01U, 0x81U, 0x03U}, kEnd}),
         {no_id(5U)},
         AOAHID_OK,
         nullptr},
        {"one report in two top-level collections",
         join({open_application(), eight_bits(), kEnd, open_application(), eight_bits(), kEnd}),
         {no_id(2U)},
         AOAHID_OK,
         nullptr},
        {"buffered bytes off a byte boundary",
         join({open_application(),
               {0x75U, 0x01U, 0x95U, 0x01U, 0x81U, 0x03U, 0x75U, 0x08U, 0x95U, 0x01U, 0x09U, 0x01U,
                0x82U, 0x02U, 0x01U},
               kEnd}),
         {no_id(2U)},
         AOAHID_OK,
         nullptr},
        {"short item cut off",
         join({open_application(), eight_bits(), kEnd, {0x05U}}),
         {no_id(1U)},
         AOAHID_ERR_PARAM,
         "raw.descriptor"},
        {"long item",
         join({open_application(), eight_bits(), kEnd, {0xFEU, 0x00U, 0x00U}}),
         {no_id(1U)},
         AOAHID_OK,
         nullptr},
        {"long item cut off",
         join({open_application(), eight_bits(), kEnd, {0xFEU, 0x04U, 0x00U, 0x01U}}),
         {no_id(1U)},
         AOAHID_ERR_PARAM,
         "raw.descriptor"},
        {"reserved item type",
         join({open_application(), {0x0CU}, eight_bits(), kEnd}),
         {no_id(1U)},
         AOAHID_OK,
         nullptr},
    };
}

void test_raw_descriptor_cases() {
    for (const Case& entry : cases()) {
        const aoa::hid::ValidationIssue found = aoa::hid::validate_raw(
            entry.descriptor.data(), entry.descriptor.size(),
            entry.reports.empty() ? nullptr : entry.reports.data(), entry.reports.size());
        const bool field_matches =
            entry.field == nullptr
                ? found.field == nullptr
                : found.field != nullptr && std::strcmp(found.field, entry.field) == 0;
        if (found.result != entry.result || !field_matches) {
            std::fprintf(stderr, "raw descriptor case '%s': result %d field %s\n", entry.name,
                         static_cast<int>(found.result),
                         found.field == nullptr ? "(none)" : found.field);
        }
        AOAHID_CHECK(found.result == entry.result && field_matches);
    }
}

void test_raw_descriptor_limits() {
    const auto valid = join({open_application(), eight_bits(), kEnd});
    const aoahid_raw_report report = no_id(1U);
    AOAHID_CHECK(aoa::hid::validate_raw(nullptr, 0U, &report, 1U).result == AOAHID_ERR_UNSET_FIELD);
    // The AOA wire-length field is 16 bits.
    std::vector<std::uint8_t> huge(65536U, 0x00U);
    const aoa::hid::ValidationIssue too_long =
        aoa::hid::validate_raw(huge.data(), huge.size(), &report, 1U);
    AOAHID_CHECK(too_long.result == AOAHID_ERR_OVERFLOW &&
                 std::strcmp(too_long.field, "raw.descriptor") == 0);
    // More than 256 entries cannot all be distinct Report IDs.
    const std::vector<aoahid_raw_report> table(257U, report);
    AOAHID_CHECK(
        aoa::hid::validate_raw(valid.data(), valid.size(), table.data(), table.size()).result ==
        AOAHID_ERR_OVERFLOW);
    // A valid descriptor reports what a target parser must support.
    aoa::hid::DescriptorRequirements requirements{};
    std::array<std::uint8_t, 1U> mask{};
    AOAHID_CHECK(
        aoa::hid::validate_raw(valid.data(), valid.size(), &report, 1U, mask.data(), &requirements)
            .result == AOAHID_OK);
    AOAHID_CHECK(requirements.maximum_fields_per_report == 1U &&
                 requirements.maximum_report_size_bits == 1U && mask[0] == 0xFFU);
}

} // namespace

// Called by the item_writer suite.
void test_raw_validator() {
    test_target_neutral_raw_requirements();
    test_local_state_and_delimiters();
    test_raw_arithmetic_and_failure_paths();
    test_structure_the_target_decides();
    test_usage_ranges_and_local_items();
}

void test_raw_descriptor() {
    test_raw_descriptor_cases();
    test_raw_descriptor_limits();
}
