// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * The keyboard profile: its key mutator.
 */

#include "profiles/state_support.hpp"

using namespace aoa::state_build;

extern "C" {

// The usage/down order is the fixed, already-shipped aoahid_keyboard_key(node, usage,
// down) ABI order and cannot be changed to avoid this warning.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static aoahid_result keyboard_key_impl(aoahid_node* node, const std::uint16_t usage,
                                       const std::uint32_t down) {
    aoa::detail::clear_error();
    if (!aoa::detail::valid_boolean(down)) {
        set_error(AOAHID_ERR_PARAM, "keyboard.key", "down must be exactly zero or one.");
        return AOAHID_ERR_PARAM;
    }
    const aoahid_result check = usable(node, AOAHID_PROFILE_KEYBOARD, "keyboard.key");
    if (check != AOAHID_OK)
        return check;
    const bool press = down == 1U;
    auto* keyboard = state<aoa::detail::KeyboardState>(node);
    const auto* spec = config<aoa::detail::KeyboardConfig>(node);
    bool changed = false;
    if (usage >= aoa::hid::usage::keyboard_left_control &&
        usage <= aoa::hid::usage::keyboard_right_gui) {
        const auto mask =
            static_cast<std::uint8_t>(1U << (usage - aoa::hid::usage::keyboard_left_control));
        changed = ((keyboard->modifiers & mask) != 0U) != press;
        if (changed && (keyboard->modifier_transitions & mask) != 0U) {
            return pending_transition("keyboard.key");
        }
        if (changed) {
            keyboard->modifiers = press ? static_cast<std::uint8_t>(keyboard->modifiers | mask)
                                        : static_cast<std::uint8_t>(keyboard->modifiers & ~mask);
            keyboard->modifier_transitions =
                static_cast<std::uint8_t>(keyboard->modifier_transitions | mask);
        }
    } else {
        if (usage < spec->options.usage_minimum || usage > spec->options.usage_maximum) {
            set_error(AOAHID_ERR_PARAM, "keyboard.usage",
                      "The Usage is outside the caller-declared key range.");
            return AOAHID_ERR_PARAM;
        }
        const std::size_t index = usage - spec->options.usage_minimum;
        if (index >= keyboard->key_transitions.size()) {
            set_error(AOAHID_ERR_INTERNAL, "keyboard.key",
                      "The preallocated keyboard state does not match the declared Usage range.");
            return AOAHID_ERR_INTERNAL;
        }
        if (index >= keyboard->pressed_bitmap.size()) {
            set_error(AOAHID_ERR_INTERNAL, "keyboard.key",
                      "The preallocated bitmap does not match the declared Usage range.");
            return AOAHID_ERR_INTERNAL;
        }
        changed = (keyboard->pressed_bitmap[index] != 0U) != press;
        if (changed && keyboard->key_transitions[index] != 0U) {
            return pending_transition("keyboard.key");
        }
        if (changed) {
            if (press) {
                if (keyboard->pressed_bitmap_count >= keyboard->pressed_bitmap.size()) {
                    set_error(AOAHID_ERR_INTERNAL, "keyboard.key",
                              "The bitmap key count is inconsistent with its storage.");
                    return AOAHID_ERR_INTERNAL;
                }
                keyboard->pressed_bitmap[index] = 1U;
                ++keyboard->pressed_bitmap_count;
            } else {
                if (keyboard->pressed_bitmap_count == 0U) {
                    set_error(AOAHID_ERR_INTERNAL, "keyboard.key",
                              "The bitmap key count is inconsistent with its storage.");
                    return AOAHID_ERR_INTERNAL;
                }
                keyboard->pressed_bitmap[index] = 0U;
                --keyboard->pressed_bitmap_count;
            }
            keyboard->key_transitions[index] = 1U;
        }
    }
    node->dirty = node->dirty || changed;
    aoa::detail::clear_error();
    return AOAHID_OK;
}

aoahid_result AOAHID_CALL aoahid_keyboard_key(aoahid_node* node, const std::uint16_t usage,
                                              const std::uint32_t down) {
    return abi_state_result(node, "keyboard.key",
                            [&] { return keyboard_key_impl(node, usage, down); });
}

} // extern "C"
