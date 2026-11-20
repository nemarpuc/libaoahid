// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * The gamepad profile: its button, axis and hat mutators.
 */

#include "profiles/state_support.hpp"

using namespace aoa::state_build;

extern "C" {

static aoahid_result aoahid_gamepad_button_impl(aoahid_node* node, const std::uint32_t button,
                                                const std::uint32_t down) {
    aoa::detail::clear_error();
    const aoahid_result check = usable(node, AOAHID_PROFILE_GAMEPAD, "gamepad.button");
    if (check != AOAHID_OK)
        return check;
    auto* gamepad = state<aoa::detail::GamepadState>(node);
    if (button == 0U || button > gamepad->buttons.size() || !aoa::detail::valid_boolean(down)) {
        set_error(AOAHID_ERR_PARAM, "gamepad.button",
                  "Button is one-based and down must be zero or one.");
        return AOAHID_ERR_PARAM;
    }
    if (!transition_storage_matches(gamepad->buttons, gamepad->button_transitions,
                                    "gamepad.button")) {
        return AOAHID_ERR_INTERNAL;
    }
    const std::size_t index = button - 1U;
    const auto value = static_cast<std::uint8_t>(down);
    const bool changed = gamepad->buttons[index] != value;
    if (changed && gamepad->button_transitions[index] != 0U) {
        return pending_transition("gamepad.button");
    }
    if (changed) {
        gamepad->buttons[index] = value;
        gamepad->button_transitions[index] = 1U;
    }
    node->dirty = node->dirty || changed;
    return AOAHID_OK;
}

static aoahid_result aoahid_gamepad_axis_impl(aoahid_node* node, const std::size_t axis_index,
                                              const std::int32_t value) {
    aoa::detail::clear_error();
    const aoahid_result check = usable(node, AOAHID_PROFILE_GAMEPAD, "gamepad.axis");
    if (check != AOAHID_OK)
        return check;
    auto* gamepad = state<aoa::detail::GamepadState>(node);
    const auto* spec = config<aoa::detail::GamepadConfig>(node);
    if (axis_index >= gamepad->axes.size() || !in_range(value, spec->axes[axis_index].axis.value)) {
        set_error(AOAHID_ERR_PARAM, "gamepad.axis", "The axis index or value is outside the spec.");
        return AOAHID_ERR_PARAM;
    }
    const bool changed = gamepad->axes[axis_index] != value;
    gamepad->axes[axis_index] = value;
    node->dirty = node->dirty || changed;
    return AOAHID_OK;
}

static aoahid_result gamepad_dpad_impl(aoahid_node* node, const std::uint32_t up,
                                       const std::uint32_t down, const std::uint32_t right,
                                       const std::uint32_t left) {
    aoa::detail::clear_error();
    const aoahid_result check = usable(node, AOAHID_PROFILE_GAMEPAD, "gamepad.dpad");
    if (check != AOAHID_OK)
        return check;
    const auto* spec = config<aoa::detail::GamepadConfig>(node);
    if ((spec->options.dpad_representation != AOAHID_DPAD_BUTTONS &&
         spec->options.dpad_representation != AOAHID_DPAD_HAT) ||
        !aoa::detail::valid_boolean(up) || !aoa::detail::valid_boolean(down) ||
        !aoa::detail::valid_boolean(right) || !aoa::detail::valid_boolean(left)) {
        set_error(AOAHID_ERR_PARAM, "gamepad.dpad",
                  "The spec must select a D-pad representation and every direction must be zero "
                  "or one.");
        return AOAHID_ERR_PARAM;
    }
    const bool any_direction = (up | down | right | left) != 0U;
    std::int32_t encoded =
        static_cast<std::int32_t>(up | (down << 1U) | (right << 2U) | (left << 3U));
    if (spec->options.dpad_representation == AOAHID_DPAD_HAT &&
        ((up == 1U && down == 1U) || (right == 1U && left == 1U))) {
        set_error(AOAHID_ERR_PARAM, "gamepad.dpad",
                  "A Hat Switch cannot encode opposite directions simultaneously.");
        return AOAHID_ERR_PARAM;
    }
    if (spec->options.dpad_representation == AOAHID_DPAD_HAT && encoded != 0) {
        if (up == 1U) {
            encoded = right == 1U ? 1 : (left == 1U ? 7 : 0);
        } else if (down == 1U) {
            encoded = right == 1U ? 3 : (left == 1U ? 5 : 4);
        } else {
            encoded = right == 1U ? 2 : 6;
        }
    }
    auto* gamepad = state<aoa::detail::GamepadState>(node);
    const bool next_has_direction = any_direction;
    const bool changed =
        gamepad->hat_has_direction != next_has_direction || gamepad->hat != encoded;
    if (changed && gamepad->hat_transition_pending) {
        return pending_transition("gamepad.dpad");
    }
    if (changed) {
        gamepad->hat_has_direction = next_has_direction;
        gamepad->hat = encoded;
        gamepad->hat_transition_pending = true;
    }
    node->dirty = node->dirty || changed;
    return AOAHID_OK;
}

aoahid_result AOAHID_CALL aoahid_gamepad_button(aoahid_node* node, const std::uint32_t button,
                                                const std::uint32_t down) {
    return abi_state_result(node, "gamepad.button",
                            [&] { return aoahid_gamepad_button_impl(node, button, down); });
}

aoahid_result AOAHID_CALL aoahid_gamepad_axis(aoahid_node* node, const std::size_t axis_index,
                                              const std::int32_t value) {
    return abi_state_result(node, "gamepad.axis",
                            [&] { return aoahid_gamepad_axis_impl(node, axis_index, value); });
}

aoahid_result AOAHID_CALL aoahid_gamepad_dpad(aoahid_node* node, const std::uint32_t up,
                                              const std::uint32_t down, const std::uint32_t right,
                                              const std::uint32_t left) {
    return abi_state_result(node, "gamepad.dpad",
                            [&] { return gamepad_dpad_impl(node, up, down, right, left); });
}

} // extern "C"
