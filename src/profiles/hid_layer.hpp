// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
#pragma once

/*
 * The HID layer of a registration: the descriptor of the Nodes that share one AOA HID ID, joined,
 * and the rules they must follow to be joined. Pure code: no I/O and no Device state.
 */

#include "aoahid.h"
#include "hid/validator.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace aoa::detail {

// Why a set of Specs cannot share a registration. Every pointer is a static string.
struct JoinRefusal {
    aoahid_result result{AOAHID_OK};
    const char* field{};
    const char* reason{};
    std::uint16_t report_id{};
};

// The descriptor the phone is given for a registration, and what it needs of the Linux parser.
struct JoinedDescriptor {
    std::vector<std::uint8_t> bytes;
    aoa::hid::DescriptorRequirements requirements;
};

// True when the descriptor leaves its global state as a descriptor starts: Unit, Unit Exponent,
// Physical Minimum and Physical Maximum at zero and no pushed state (HID 1.11 6.2.2.7). Only a
// descriptor that does can be followed by another in the same report descriptor without the next
// one inheriting these items.
bool ends_with_clean_globals(const std::vector<std::uint8_t>& descriptor) noexcept;

// Joins the descriptors of the Specs, in the order given. One Spec is joined as it is. Two or more
// need Report IDs in every Spec, different across them, at most one battery report, and clean
// globals at the end of every Spec but the last. On a refusal nothing is written to out.
aoahid_result join_descriptors(const std::vector<const struct aoahid_spec*>& specs,
                               JoinedDescriptor* out, JoinRefusal* refusal);

} // namespace aoa::detail
