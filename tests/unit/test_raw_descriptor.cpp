// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* The raw-descriptor validator refuses each malformed descriptor with the
 * result and field that name its fault, and refuses a report table that does
 * not list exactly the descriptor's Input reports. */
#include "hid/validator.hpp"
#include "test.hpp"

#include <array>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {

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
        {"empty table", valid, {}, AOAHID_ERR_UNSET_FIELD, "raw.reports"},
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
         AOAHID_ERR_PARAM,
         "raw.usage_page"},
        {"report ID zero",
         join({open_application(), {0x85U, 0x00U}, eight_bits(), kEnd}),
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
         AOAHID_ERR_PARAM,
         "raw.global_stack"},
        {"push without pop",
         join({open_application(), {0xA4U}, eight_bits(), kEnd}),
         {no_id(1U)},
         AOAHID_ERR_PARAM,
         "raw.descriptor"},
        {"end collection first",
         join({kEnd, open_application(), eight_bits(), kEnd}),
         {no_id(1U)},
         AOAHID_ERR_PARAM,
         "raw.collection"},
        {"collection never closed",
         join({open_application(), eight_bits()}),
         {no_id(1U)},
         AOAHID_ERR_PARAM,
         "raw.descriptor"},
        {"top level is a Physical collection",
         join({{0x05U, 0x01U, 0x09U, 0x01U, 0xA1U, 0x00U}, eight_bits(), kEnd}),
         {no_id(1U)},
         AOAHID_ERR_PARAM,
         "raw.collection"},
        {"no collection at all",
         {0x05U, 0x01U, 0x09U, 0x06U},
         {no_id(1U)},
         AOAHID_ERR_PARAM,
         "raw.collection"},
        {"input outside a collection",
         join({eight_bits(), open_application(), kEnd}),
         {no_id(1U)},
         AOAHID_ERR_PARAM,
         "raw.input"},
        {"input without a report count",
         join({open_application(), {0x75U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U}, kEnd}),
         {no_id(1U)},
         AOAHID_ERR_PARAM,
         "raw.input"},
        {"report size over 32 bits",
         join({open_application(), {0x75U, 0x21U, 0x95U, 0x01U, 0x81U, 0x03U}, kEnd}),
         {no_id(5U)},
         AOAHID_ERR_PARAM,
         "raw.input"},
        {"one report in two top-level collections",
         join({open_application(), eight_bits(), kEnd, open_application(), eight_bits(), kEnd}),
         {no_id(2U)},
         AOAHID_ERR_PARAM,
         "raw.input"},
        {"buffered bytes off a byte boundary",
         join({open_application(),
               {0x75U, 0x01U, 0x95U, 0x01U, 0x81U, 0x03U, 0x75U, 0x08U, 0x95U, 0x01U, 0x09U, 0x01U,
                0x82U, 0x02U, 0x01U},
               kEnd}),
         {no_id(2U)},
         AOAHID_ERR_PARAM,
         "raw.input"},
        {"short item cut off",
         join({open_application(), eight_bits(), kEnd, {0x05U}}),
         {no_id(1U)},
         AOAHID_ERR_PARAM,
         "raw.descriptor"},
        {"long item",
         join({open_application(), eight_bits(), kEnd, {0xFEU, 0x00U, 0x00U}}),
         {no_id(1U)},
         AOAHID_ERR_UNSUPPORTED,
         "raw.descriptor"},
        {"reserved item type",
         join({open_application(), {0x0CU}, eight_bits(), kEnd}),
         {no_id(1U)},
         AOAHID_ERR_PARAM,
         "raw.item_type"},
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

void test_raw_descriptor() {
    test_raw_descriptor_cases();
    test_raw_descriptor_limits();
}
