// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * The rules for joining the descriptors of the Nodes of one registration. The same rules, worded
 * the same, as libmimusb's compose_hid_groups for Nodes that share an interface, plus the ones
 * that come from AOA registering one HID device per registration: one battery, and clean globals.
 */

#include "profiles/hid_layer.hpp"

#include "api/internal.hpp"

#include <algorithm>
#include <bitset>

namespace aoa::detail {

namespace {

// HID 1.11 6.2.2.2 short items: bits 1..0 the size (3 is four bytes), bits 3..2 the type, bits
// 7..4 the tag. 0xFE is a long item, which has no global tag.
constexpr std::uint8_t kLongItem = 0xFEU;
constexpr std::uint8_t kTypeGlobal = 1U;

constexpr std::uint8_t kPhysicalMinimum = 3U;
constexpr std::uint8_t kPhysicalMaximum = 4U;
constexpr std::uint8_t kUnitExponent = 5U;
constexpr std::uint8_t kUnit = 6U;
constexpr std::uint8_t kPush = 10U;
constexpr std::uint8_t kPop = 11U;

struct Globals {
    std::int64_t physical_minimum{};
    std::int64_t physical_maximum{};
    std::int64_t unit_exponent{};
    std::int64_t unit{};

    bool clean() const noexcept {
        return physical_minimum == 0 && physical_maximum == 0 && unit_exponent == 0 && unit == 0;
    }
};

std::int64_t item_value(const std::uint8_t* data, const std::size_t size) noexcept {
    std::int64_t value = 0;
    for (std::size_t index = 0U; index < size; ++index) {
        value |= static_cast<std::int64_t>(data[index]) << (8U * index);
    }
    return value;
}

void collect_report_ids(const aoahid_spec* spec, std::bitset<256>* ids, std::size_t* count,
                        bool* duplicate_inside, std::uint16_t* duplicate_id) noexcept {
    const auto add = [&](const std::uint8_t id) {
        if (ids->test(id) && !*duplicate_inside) {
            *duplicate_inside = true;
            *duplicate_id = id;
        }
        ids->set(id);
        ++*count;
    };
    if (const auto* raw = std::get_if<RawConfig>(&spec->config)) {
        for (const aoahid_raw_report& report : raw->reports) {
            if (report.report_id.enabled == 1U) {
                add(report.report_id.value);
            }
        }
        return;
    }
    if (spec->layout.has_report_id) {
        add(spec->layout.report_id);
    }
    if (spec->has_battery && spec->battery_layout.has_report_id) {
        add(spec->battery_layout.report_id);
    }
}

JoinRefusal refusal(const aoahid_result result, const char* field, const char* reason,
                    const std::uint16_t report_id = 0U) noexcept {
    return JoinRefusal{result, field, reason, report_id};
}

} // namespace

bool ends_with_clean_globals(const std::vector<std::uint8_t>& descriptor) noexcept {
    Globals current;
    std::vector<Globals> pushed;
    std::size_t at = 0U;
    while (at < descriptor.size()) {
        const std::uint8_t prefix = descriptor[at];
        if (prefix == kLongItem) {
            // A long item is a size, a tag and that many bytes.
            if (at + 2U >= descriptor.size()) {
                return false;
            }
            at += 3U + descriptor[at + 1U];
            continue;
        }
        const std::size_t size = (prefix & 0x03U) == 3U ? 4U : (prefix & 0x03U);
        const std::uint8_t type = static_cast<std::uint8_t>((prefix >> 2U) & 0x03U);
        const std::uint8_t tag = static_cast<std::uint8_t>(prefix >> 4U);
        if (at + 1U + size > descriptor.size()) {
            return false;
        }
        if (type == kTypeGlobal) {
            const std::uint8_t* data = descriptor.data() + at + 1U;
            // Physical and Unit values are signed in the spec; only whether they are zero matters.
            const std::int64_t value = item_value(data, size);
            switch (tag) {
            case kPhysicalMinimum:
                current.physical_minimum = value;
                break;
            case kPhysicalMaximum:
                current.physical_maximum = value;
                break;
            case kUnitExponent:
                current.unit_exponent = value;
                break;
            case kUnit:
                current.unit = value;
                break;
            case kPush:
                pushed.push_back(current);
                break;
            case kPop:
                if (pushed.empty()) {
                    return false;
                }
                current = pushed.back();
                pushed.pop_back();
                break;
            default:
                break;
            }
        }
        at += 1U + size;
    }
    return pushed.empty() && current.clean();
}

aoahid_result join_descriptors(const std::vector<const aoahid_spec*>& specs, JoinedDescriptor* out,
                               JoinRefusal* refusal_out) {
    JoinedDescriptor joined;
    if (specs.size() == 1U) {
        joined.bytes = specs.front()->descriptor;
        joined.requirements = specs.front()->requirements;
        *out = std::move(joined);
        return AOAHID_OK;
    }

    std::bitset<256> seen;
    std::size_t batteries = 0U;
    for (std::size_t index = 0U; index < specs.size(); ++index) {
        const aoahid_spec* spec = specs[index];
        std::bitset<256> own;
        std::size_t own_count = 0U;
        bool duplicate_inside = false;
        std::uint16_t duplicate_id = 0U;
        collect_report_ids(spec, &own, &own_count, &duplicate_inside, &duplicate_id);
        // HID 1.11 5.6 and 6.2.2.7: once a Report ID appears in a descriptor, every report of the
        // device starts with one, so every Node of a registration must declare its own.
        if (own_count == 0U) {
            *refusal_out = refusal(AOAHID_ERR_PARAM, "node.report_id",
                                   "Multiple nodes sharing a registration must all declare Report "
                                   "IDs.");
            return AOAHID_ERR_PARAM;
        }
        for (std::size_t id = 0U; id < own.size(); ++id) {
            if (own.test(id) && seen.test(id)) {
                *refusal_out = refusal(AOAHID_ERR_PARAM, "node.report_id",
                                       "Duplicate Report ID assigned in the same registration.",
                                       static_cast<std::uint16_t>(id));
                return AOAHID_ERR_PARAM;
            }
        }
        seen |= own;
        if (spec->has_battery) {
            ++batteries;
        }
        // Globals carry over to the items that follow (HID 1.11 6.2.2.7): a member may be followed
        // by another only when it leaves Unit, Physical and Push as a descriptor starts.
        if (index + 1U < specs.size() && !ends_with_clean_globals(spec->descriptor)) {
            *refusal_out = refusal(AOAHID_ERR_UNSUPPORTED, "spec.descriptor.globals",
                                   "A Spec that leaves a Unit, Physical item or Push open cannot "
                                   "be followed by another in a registration.");
            return AOAHID_ERR_UNSUPPORTED;
        }
        joined.bytes.insert(joined.bytes.end(), spec->descriptor.begin(), spec->descriptor.end());
        auto& needs = joined.requirements;
        const auto& own_needs = spec->requirements;
        needs.maximum_fields_per_report =
            std::max(needs.maximum_fields_per_report, own_needs.maximum_fields_per_report);
        needs.maximum_global_stack_depth =
            std::max(needs.maximum_global_stack_depth, own_needs.maximum_global_stack_depth);
        needs.maximum_report_size_bits =
            std::max(needs.maximum_report_size_bits, own_needs.maximum_report_size_bits);
        needs.maximum_usages = std::max(needs.maximum_usages, own_needs.maximum_usages);
        needs.maximum_report_data_bits =
            std::max(needs.maximum_report_data_bits, own_needs.maximum_report_data_bits);
    }
    // Mainline Linux v5.9 to v6.18 keeps one battery per HID device (docs/LIMITS.md).
    if (batteries > 1U) {
        *refusal_out = refusal(AOAHID_ERR_PARAM, "node.battery",
                               "At most one Node of a registration may carry a battery report.");
        return AOAHID_ERR_PARAM;
    }
    *out = std::move(joined);
    return AOAHID_OK;
}

} // namespace aoa::detail
