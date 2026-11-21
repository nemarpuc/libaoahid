// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Test-only helpers for opening Nodes: a placement and a pair of waits written as one expression.
 * They never enter the library ABI. */
#pragma once

#include "aoahid.h"

#include <array>
#include <cstddef>
#include <cstdint>

// A placement with a registration number of its own, so Nodes opened one after another are
// separate HID devices (the way Nodes were before registrations could be shared). A test that
// wants a shared registration builds its placement by hand. The structure lives in a ring of
// static slots, so a call may be written inline in an argument list.
inline const aoahid_node_placement* separate_placement(const std::uint32_t reserve_slot = 0U) {
    static std::array<aoahid_node_placement, 64U> ring{};
    static std::size_t at = 0U;
    static std::uint32_t next_registration = 1000U;
    aoahid_node_placement& placement = ring[at++ % ring.size()];
    placement = aoahid_node_placement{static_cast<std::uint32_t>(sizeof(placement)), 0U,
                                      next_registration++, reserve_slot};
    return &placement;
}

// Both waits of a commit, kept in a ring of static slots like separate_placement.
inline const aoahid_commit_timeouts* commit_timeouts_of(const std::uint32_t control_timeout_ms,
                                                        const std::uint32_t drain_timeout_ms) {
    static std::array<aoahid_commit_timeouts, 64U> ring{};
    static std::size_t at = 0U;
    aoahid_commit_timeouts& timeouts = ring[at++ % ring.size()];
    timeouts = aoahid_commit_timeouts{static_cast<std::uint32_t>(sizeof(timeouts)), 0U,
                                      control_timeout_ms, drain_timeout_ms};
    return &timeouts;
}
