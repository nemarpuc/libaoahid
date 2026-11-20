// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * The mouse profile: its movement, scroll and button mutators.
 */

#include "profiles/state_support.hpp"

using namespace aoa::state_build;

extern "C" {

static aoahid_result aoahid_mouse_move_impl(aoahid_node* node, const std::int32_t dx,
                                            const std::int32_t dy) {
    aoa::detail::clear_error();
    const aoahid_result check = usable(node, AOAHID_PROFILE_MOUSE, "mouse.move");
    if (check != AOAHID_OK)
        return check;
    auto* mouse = state<aoa::detail::MouseState>(node);
    std::int64_t next_dx = mouse->dx;
    std::int64_t next_dy = mouse->dy;
    if (!add_checked(&next_dx, dx) || !add_checked(&next_dy, dy)) {
        set_error(AOAHID_ERR_OVERFLOW, "mouse.delta",
                  "The pending relative movement overflowed int64_t.");
        return AOAHID_ERR_OVERFLOW;
    }
    mouse->dx = next_dx;
    mouse->dy = next_dy;
    node->dirty = node->dirty || dx != 0 || dy != 0;
    return AOAHID_OK;
}

static aoahid_result aoahid_mouse_scroll_impl(aoahid_node* node, const std::int32_t wheel,
                                              const std::int32_t pan) {
    aoa::detail::clear_error();
    const aoahid_result check = usable(node, AOAHID_PROFILE_MOUSE, "mouse.scroll");
    if (check != AOAHID_OK)
        return check;
    auto* mouse = state<aoa::detail::MouseState>(node);
    const auto* spec = config<aoa::detail::MouseConfig>(node);
    if ((wheel != 0 && spec->options.enable_wheel != 1U) ||
        (pan != 0 && spec->options.enable_pan != 1U)) {
        set_error(AOAHID_ERR_UNSUPPORTED, "mouse.scroll",
                  "The requested scroll axis is absent from the spec.");
        return AOAHID_ERR_UNSUPPORTED;
    }
    std::int64_t next_wheel = mouse->wheel;
    std::int64_t next_pan = mouse->pan;
    if (!add_checked(&next_wheel, wheel) || !add_checked(&next_pan, pan)) {
        set_error(AOAHID_ERR_OVERFLOW, "mouse.scroll",
                  "The pending relative scroll overflowed int64_t.");
        return AOAHID_ERR_OVERFLOW;
    }
    mouse->wheel = next_wheel;
    mouse->pan = next_pan;
    node->dirty = node->dirty || wheel != 0 || pan != 0;
    return AOAHID_OK;
}

static aoahid_result aoahid_mouse_button_impl(aoahid_node* node, const std::uint32_t button,
                                              const std::uint32_t down) {
    aoa::detail::clear_error();
    const aoahid_result check = usable(node, AOAHID_PROFILE_MOUSE, "mouse.button");
    if (check != AOAHID_OK)
        return check;
    auto* mouse = state<aoa::detail::MouseState>(node);
    if (button == 0U || button > mouse->buttons.size() || !aoa::detail::valid_boolean(down)) {
        set_error(AOAHID_ERR_PARAM, "mouse.button",
                  "Button is one-based and down must be zero or one.");
        return AOAHID_ERR_PARAM;
    }
    if (!transition_storage_matches(mouse->buttons, mouse->button_transitions, "mouse.button")) {
        return AOAHID_ERR_INTERNAL;
    }
    const std::size_t index = button - 1U;
    const auto value = static_cast<std::uint8_t>(down);
    const bool changed = mouse->buttons[index] != value;
    if (changed && mouse->button_transitions[index] != 0U) {
        return pending_transition("mouse.button");
    }
    if (changed) {
        mouse->buttons[index] = value;
        mouse->button_transitions[index] = 1U;
    }
    node->dirty = node->dirty || changed;
    return AOAHID_OK;
}

aoahid_result AOAHID_CALL aoahid_mouse_move(aoahid_node* node, const std::int32_t dx,
                                            const std::int32_t dy) {
    return abi_state_result(node, "mouse.move",
                            [&] { return aoahid_mouse_move_impl(node, dx, dy); });
}

aoahid_result AOAHID_CALL aoahid_mouse_scroll(aoahid_node* node, const std::int32_t wheel,
                                              const std::int32_t pan) {
    return abi_state_result(node, "mouse.scroll",
                            [&] { return aoahid_mouse_scroll_impl(node, wheel, pan); });
}

aoahid_result AOAHID_CALL aoahid_mouse_button(aoahid_node* node, const std::uint32_t button,
                                              const std::uint32_t down) {
    return abi_state_result(node, "mouse.button",
                            [&] { return aoahid_mouse_button_impl(node, button, down); });
}

} // extern "C"
