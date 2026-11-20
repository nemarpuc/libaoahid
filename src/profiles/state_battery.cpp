// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * The battery report any generated profile can carry: its mutator.
 */

#include "profiles/state_support.hpp"

using namespace aoa::state_build;

extern "C" {

static aoahid_result aoahid_battery_update_impl(aoahid_node* node, const std::int32_t strength) {
    aoa::detail::clear_error();
    if (node == nullptr || node->closed || node->spec == nullptr || !node->spec->has_battery) {
        set_error(AOAHID_ERR_PARAM, "battery.update", "The node has no battery report.");
        return AOAHID_ERR_PARAM;
    }
    const aoahid_result link = link_available(node, "battery.update");
    if (link != AOAHID_OK)
        return link;
    if (node->transfer_inflight.load(std::memory_order_acquire)) {
        set_error(AOAHID_ERR_BUSY, "battery.update", "The node has one report in flight.");
        return AOAHID_ERR_BUSY;
    }
    if (!in_range(strength, node->spec->battery.strength)) {
        set_error(AOAHID_ERR_PARAM, "battery.strength", "The value is outside the declared range.");
        return AOAHID_ERR_PARAM;
    }
    // The first value is always a change, even when it equals the zero the
    // state starts with.
    const bool changed = !node->battery.has_value || node->battery.strength != strength;
    node->battery.has_value = true;
    node->battery.strength = strength;
    node->battery_dirty = node->battery_dirty || changed;
    return AOAHID_OK;
}

aoahid_result AOAHID_CALL aoahid_battery_update(aoahid_node* node, const std::int32_t strength) {
    return abi_state_result(node, "battery.update",
                            [&] { return aoahid_battery_update_impl(node, strength); });
}

} // extern "C"
