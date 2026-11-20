// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * The toggle profile: its usage mutator.
 */

#include "profiles/state_support.hpp"

using namespace aoa::state_build;

extern "C" {

// The usage/down order is the fixed, already-shipped aoahid_toggle_usage(node,
// usage, down) ABI order and cannot be changed to avoid this warning.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static aoahid_result aoahid_toggle_impl(aoahid_node* node, const std::uint16_t usage,
                                        const std::uint32_t down) {
    aoa::detail::clear_error();
    if (!aoa::detail::valid_boolean(down)) {
        set_error(AOAHID_ERR_PARAM, "toggle", "down must be exactly zero or one.");
        return AOAHID_ERR_PARAM;
    }
    if (node == nullptr || node->spec == nullptr || node->closed ||
        node->spec->kind != AOAHID_PROFILE_TOGGLE) {
        set_error(AOAHID_ERR_PARAM, "toggle", "The operation requires a live Toggle node.");
        return AOAHID_ERR_PARAM;
    }
    const aoahid_result link = link_available(node, "toggle");
    if (link != AOAHID_OK)
        return link;
    if (node->transfer_inflight.load(std::memory_order_acquire)) {
        set_error(AOAHID_ERR_BUSY, "toggle", "The node has one report in flight.");
        return AOAHID_ERR_BUSY;
    }
    auto* consumer = state<aoa::detail::ConsumerState>(node);
    const auto* spec = config<aoa::detail::ConsumerConfig>(node);
    const auto found = std::find(spec->allowed_usages.begin(), spec->allowed_usages.end(), usage);
    if (found == spec->allowed_usages.end()) {
        set_error(AOAHID_ERR_PARAM, "toggle.usage", "The Usage is not in the caller allow-list.");
        return AOAHID_ERR_PARAM;
    }
    if (!transition_storage_matches(consumer->pressed, consumer->transitions, "toggle")) {
        return AOAHID_ERR_INTERNAL;
    }
    const auto index = static_cast<std::size_t>(found - spec->allowed_usages.begin());
    const auto value = static_cast<std::uint8_t>(down);
    const bool changed = consumer->pressed[index] != value;
    if (changed && consumer->transitions[index] != 0U) {
        return pending_transition("toggle");
    }
    if (changed) {
        consumer->pressed[index] = value;
        consumer->transitions[index] = 1U;
    }
    node->dirty = node->dirty || changed;
    return AOAHID_OK;
}

aoahid_result AOAHID_CALL aoahid_toggle_usage(aoahid_node* node, const std::uint16_t usage,
                                              const std::uint32_t down) {
    return abi_state_result(node, "toggle", [&] { return aoahid_toggle_impl(node, usage, down); });
}

} // extern "C"
