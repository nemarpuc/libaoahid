// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * The touchscreen and touchpad profiles: their contact and button mutators.
 */

#include "profiles/state_support.hpp"

using namespace aoa::state_build;

extern "C" {

// The optional fields of aoahid_touchscreen_extra and aoahid_touchpad_extra, which have the
// same layout, so the two public entry points share one implementation.
struct TouchExtra {
    std::int32_t pressure;
    std::int32_t width;
    std::int32_t height;
    std::int32_t azimuth;
};

// The contact_id/down/x/y order is the fixed aoahid_touchscreen_contact and
// aoahid_touchpad_contact ABI order and cannot be changed to avoid this warning.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static aoahid_result touch_contact_impl(aoahid_node* node, const aoahid_profile_kind kind,
                                        const std::uint32_t contact_id, const std::uint32_t down,
                                        const std::int32_t x, const std::int32_t y,
                                        const TouchExtra* extra) {
    aoa::detail::clear_error();
    const bool right_kind = node != nullptr && node->spec != nullptr && node->spec->kind == kind;
    if (!right_kind || node->closed || !aoa::detail::valid_boolean(down)) {
        set_error(AOAHID_ERR_PARAM, "touch", "A live touch node and boolean down are required.");
        return AOAHID_ERR_PARAM;
    }
    const aoahid_result link = link_available(node, "touch");
    if (link != AOAHID_OK)
        return link;
    if (node->transfer_inflight.load(std::memory_order_acquire)) {
        set_error(AOAHID_ERR_BUSY, "touch", "A touch report is in flight.");
        return AOAHID_ERR_BUSY;
    }
    const auto* spec = config<aoa::detail::TouchConfig>(node);
    auto* touch = state<aoa::detail::TouchState>(node);
    if (touch->packet_cursor != 0U) {
        set_error(AOAHID_ERR_BUSY, "touch",
                  "The current multi-packet frame must finish before state changes.");
        return AOAHID_ERR_BUSY;
    }
    auto active =
        std::find_if(touch->contacts.begin(), touch->contacts.end(), [=](const auto& entry) {
            return entry.phase == ContactPhase::down && entry.sample.contact_id == contact_id;
        });
    if (down == 0U) {
        if (active == touch->contacts.end()) {
            set_error(AOAHID_ERR_PARAM,
                      kind == AOAHID_PROFILE_TOUCHSCREEN ? "touchscreen.contact_id"
                                                         : "touchpad.contact_id",
                      "Lift requires an active Contact ID.");
            return AOAHID_ERR_PARAM;
        }
        if (!in_range(x, spec->fields.x) || !in_range(y, spec->fields.y)) {
            set_error(AOAHID_ERR_PARAM, "touch", "The final coordinates are outside the spec.");
            return AOAHID_ERR_PARAM;
        }
        if (active->lifecycle_transition_pending) {
            return pending_transition("touch");
        }
        active->phase = ContactPhase::up;
        active->sample.x = x;
        active->sample.y = y;
        active->sample.pressure = 0;
        active->lifecycle_transition_pending = true;
        touch->packet_cursor = 0U;
        node->dirty = true;
        return AOAHID_OK;
    }
    aoa::detail::TouchContact contact{};
    contact.contact_id = contact_id;
    contact.x = x;
    contact.y = y;
    if (extra != nullptr) {
        contact.pressure = extra->pressure;
        contact.width = extra->width;
        contact.height = extra->height;
        contact.azimuth = extra->azimuth;
    }
    if (!validate_contact(*spec, contact, "touch"))
        return AOAHID_ERR_PARAM;
    if (active != touch->contacts.end()) {
        // The Contact ID is already down, so this call moves it.
        const bool changed =
            active->sample.x != contact.x || active->sample.y != contact.y ||
            active->sample.pressure != contact.pressure || active->sample.width != contact.width ||
            active->sample.height != contact.height || active->sample.azimuth != contact.azimuth;
        active->sample = contact;
        touch->packet_cursor = 0U;
        node->dirty = node->dirty || changed;
        return AOAHID_OK;
    }
    for (const auto& entry : touch->contacts) {
        if (entry.phase != ContactPhase::none && entry.sample.contact_id == contact_id) {
            set_error(AOAHID_ERR_BUSY,
                      kind == AOAHID_PROFILE_TOUCHSCREEN ? "touchscreen.contact_id"
                                                         : "touchpad.contact_id",
                      "The Contact ID is already active.");
            return AOAHID_ERR_BUSY;
        }
    }
    auto found = std::find_if(touch->contacts.begin(), touch->contacts.end(),
                              [](const auto& entry) { return entry.phase == ContactPhase::none; });
    if (found == touch->contacts.end() ||
        std::count_if(touch->contacts.begin(), touch->contacts.end(), [](const auto& entry) {
            return entry.phase != ContactPhase::none;
        }) >= static_cast<std::ptrdiff_t>(spec->fields.maximum_contacts)) {
        set_error(AOAHID_ERR_OVERFLOW,
                  kind == AOAHID_PROFILE_TOUCHSCREEN ? "touchscreen.maximum_contacts"
                                                     : "touchpad.maximum_contacts",
                  "No declared contact slot remains.");
        return AOAHID_ERR_OVERFLOW;
    }
    found->phase = ContactPhase::down;
    found->sample = contact;
    found->lifecycle_transition_pending = true;
    touch->packet_cursor = 0U;
    node->dirty = true;
    return AOAHID_OK;
}

static aoahid_result aoahid_touchpad_button_impl(aoahid_node* node, const std::uint32_t button,
                                                 const std::uint32_t down) {
    aoa::detail::clear_error();
    const aoahid_result check = usable(node, AOAHID_PROFILE_TOUCHPAD, "touchpad.button");
    if (check != AOAHID_OK)
        return check;
    auto* touch = state<aoa::detail::TouchState>(node);
    if (touch->packet_cursor != 0U) {
        set_error(AOAHID_ERR_BUSY, "touchpad.button",
                  "The current multi-packet frame must finish before state changes.");
        return AOAHID_ERR_BUSY;
    }
    if (button == 0U || button > touch->buttons.size() || !aoa::detail::valid_boolean(down)) {
        set_error(AOAHID_ERR_PARAM, "touchpad.button",
                  "Button is one-based and down must be zero or one.");
        return AOAHID_ERR_PARAM;
    }
    if (!transition_storage_matches(touch->buttons, touch->button_transitions, "touchpad.button")) {
        return AOAHID_ERR_INTERNAL;
    }
    const std::size_t index = button - 1U;
    const auto value = static_cast<std::uint8_t>(down);
    const bool changed = touch->buttons[index] != value;
    if (changed && touch->button_transitions[index] != 0U) {
        return pending_transition("touchpad.button");
    }
    if (changed) {
        touch->buttons[index] = value;
        touch->button_transitions[index] = 1U;
    }
    node->dirty = node->dirty || changed;
    return AOAHID_OK;
}

aoahid_result AOAHID_CALL aoahid_touchscreen_contact(aoahid_node* node,
                                                     const std::uint32_t contact_id,
                                                     const std::uint32_t down, const std::int32_t x,
                                                     const std::int32_t y,
                                                     const aoahid_touchscreen_extra* extra) {
    return abi_state_result(node, "touchscreen.contact", [&] {
        const TouchExtra values = extra == nullptr ? TouchExtra{}
                                                   : TouchExtra{extra->pressure, extra->width,
                                                                extra->height, extra->azimuth};
        return touch_contact_impl(node, AOAHID_PROFILE_TOUCHSCREEN, contact_id, down, x, y,
                                  extra == nullptr ? nullptr : &values);
    });
}

aoahid_result AOAHID_CALL aoahid_touchpad_contact(aoahid_node* node, const std::uint32_t contact_id,
                                                  const std::uint32_t down, const std::int32_t x,
                                                  const std::int32_t y,
                                                  const aoahid_touchpad_extra* extra) {
    return abi_state_result(node, "touchpad.contact", [&] {
        const TouchExtra values = extra == nullptr ? TouchExtra{}
                                                   : TouchExtra{extra->pressure, extra->width,
                                                                extra->height, extra->azimuth};
        return touch_contact_impl(node, AOAHID_PROFILE_TOUCHPAD, contact_id, down, x, y,
                                  extra == nullptr ? nullptr : &values);
    });
}

aoahid_result AOAHID_CALL aoahid_touchpad_button(aoahid_node* node, const std::uint32_t button,
                                                 const std::uint32_t down) {
    return abi_state_result(node, "touchpad.button",
                            [&] { return aoahid_touchpad_button_impl(node, button, down); });
}

} // extern "C"
