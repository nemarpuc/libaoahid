// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* The input calls (keys, buttons, axes, contacts, pen samples) refuse a Node
 * of another profile, a value outside its Spec and a change that would undo
 * one no report has carried yet; and once the phone is gone they report
 * NO_DEVICE without touching the state. */
#include "sim/sim_support.hpp"

#include <array>
#include <cstring>
#include <limits>

namespace {

using sim::kControlTimeoutMs;
using sim::kSendTimeoutMs;

bool field_is(const char* field) {
    const aoahid_error_detail* detail = aoahid_last_error();
    return detail->field != nullptr && std::strcmp(detail->field, field) == 0;
}

aoahid_node* open_node(aoahid_device* device, aoahid_spec* spec) {
    const aoahid_node_options options = sim::node_options();
    aoahid_node* node = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, spec, &options, kControlTimeoutMs, &node) == AOAHID_OK);
    aoahid_spec_release(spec);
    return node;
}

template <typename Options, typename Create> aoahid_spec* make(const Options& options, Create f) {
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(f(&options, &spec) == AOAHID_OK);
    return spec;
}

struct Nodes {
    aoahid_node* keyboard{};
    aoahid_node* mouse{};
    aoahid_node* toggle{};
    aoahid_node* gamepad{};
    aoahid_node* buttons_pad{};
    aoahid_node* touchscreen{};
    aoahid_node* touchpad{};
    aoahid_node* pen{};
    aoahid_node* digitizer{};
};

constexpr std::array<std::uint16_t, 1U> kMute{0xE2U};
constexpr std::array<aoahid_usage_semantic, 1U> kOnOff{AOAHID_USAGE_ON_OFF_TOGGLE};
constexpr std::array<const char*, 1U> kKey{"EV_KEY"};
constexpr std::array<const char*, 1U> kMuteCode{"KEY_MUTE"};

Nodes open_every_profile(aoahid_device* device) {
    Nodes nodes{};
    aoahid_keyboard_options keyboard;
    aoahid_keyboard_options_init(&keyboard);
    nodes.keyboard = open_node(device, make(keyboard, aoahid_spec_create_keyboard));
    aoahid_mouse_options mouse;
    aoahid_mouse_options_init(&mouse);
    mouse.enable_pan = 0U;
    mouse.pan = {};
    nodes.mouse = open_node(device, make(mouse, aoahid_spec_create_mouse));
    aoahid_toggle_options toggle;
    aoahid_toggle_options_init(&toggle);
    toggle.allowed_usages = kMute.data();
    toggle.allowed_usage_count = kMute.size();
    toggle.usage_semantics = kOnOff.data();
    toggle.expected_linux_event_types = kKey.data();
    toggle.expected_linux_codes = kMuteCode.data();
    nodes.toggle = open_node(device, make(toggle, aoahid_spec_create_toggle));
    aoahid_gamepad_options gamepad;
    aoahid_gamepad_options_init(&gamepad);
    nodes.gamepad = open_node(device, make(gamepad, aoahid_spec_create_gamepad));
    gamepad.dpad_representation = AOAHID_DPAD_NONE;
    gamepad.hat_logical_maximum = 0;
    gamepad.hat_bit_width = 0U;
    nodes.buttons_pad = open_node(device, make(gamepad, aoahid_spec_create_gamepad));
    aoahid_touchscreen_options touchscreen;
    aoahid_touchscreen_options_init(&touchscreen);
    touchscreen.maximum_contacts = 2U;
    touchscreen.contacts_per_report = 2U;
    touchscreen.contact_count = aoahid_integer_field_make(0, 2, 2U);
    touchscreen.enable_width = 1U;
    touchscreen.width = aoahid_integer_field_make(0, 100, 8U);
    touchscreen.enable_height = 1U;
    touchscreen.height = aoahid_integer_field_make(0, 100, 8U);
    nodes.touchscreen = open_node(device, make(touchscreen, aoahid_spec_create_touchscreen));
    aoahid_touchpad_options touchpad;
    aoahid_touchpad_options_init(&touchpad);
    nodes.touchpad = open_node(device, make(touchpad, aoahid_spec_create_touchpad));
    aoahid_pen_options pen;
    aoahid_pen_options_init(&pen);
    pen.enable_tilt = 1U;
    pen.tilt_x = aoahid_integer_field_make(-90, 90, 8U);
    pen.tilt_y = pen.tilt_x;
    pen.enable_twist_target_specific = 1U;
    pen.twist = aoahid_integer_field_make(0, 359, 16U);
    nodes.pen = open_node(device, make(pen, aoahid_spec_create_pen));
    aoahid_digitizer_options digitizer;
    aoahid_digitizer_options_init(&digitizer);
    digitizer.enable_hover = 0U;
    nodes.digitizer = open_node(device, make(digitizer, aoahid_spec_create_digitizer));
    return nodes;
}

aoahid_pen_sample touching(const std::int32_t x) {
    aoahid_pen_sample sample{};
    sample.in_range = 1U;
    sample.tip = 1U;
    sample.x = x;
    sample.y = 10;
    sample.pressure = 100;
    return sample;
}

// The digitizer sample has the pen sample's fields; null stays null.
aoahid_result digitizer_update(aoahid_node* node, const aoahid_pen_sample* pen) {
    if (pen == nullptr)
        return aoahid_digitizer_update(node, nullptr);
    const aoahid_digitizer_sample sample{
        pen->in_range, pen->tip,      pen->eraser, pen->barrel_buttons, pen->x,
        pen->y,        pen->pressure, pen->tilt_x, pen->tilt_y,         pen->twist};
    return aoahid_digitizer_update(node, &sample);
}

// Each input call refuses every Node but its own profile's.
void test_wrong_profile_is_refused(const Nodes& nodes) {
    aoahid_node* other = nodes.keyboard;
    aoahid_node* typed = nodes.mouse;
    const aoahid_pen_sample sample = touching(10);
    AOAHID_CHECK(aoahid_keyboard_key(typed, 0x04U, 1U) == AOAHID_ERR_PARAM &&
                 field_is("keyboard.key"));
    AOAHID_CHECK(aoahid_mouse_move(other, 1, 1) == AOAHID_ERR_PARAM && field_is("mouse.move"));
    AOAHID_CHECK(aoahid_mouse_scroll(other, 1, 0) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_mouse_button(other, 1U, 1U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_toggle_usage(other, kMute[0], 1U) == AOAHID_ERR_PARAM &&
                 field_is("toggle"));
    AOAHID_CHECK(aoahid_gamepad_button(other, 1U, 1U) == AOAHID_ERR_PARAM &&
                 field_is("gamepad.button"));
    AOAHID_CHECK(aoahid_gamepad_axis(other, 0U, 0) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_gamepad_dpad(other, 0U, 0U, 0U, 0U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_touchscreen_contact(other, 1U, 1U, 0, 0, nullptr) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchpad, 1U, 1U, 0, 0, nullptr) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_touchpad_contact(nodes.touchscreen, 1U, 1U, 0, 0, nullptr) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_touchpad_button(other, 1U, 1U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_pen_update(nodes.digitizer, &sample) == AOAHID_ERR_PARAM &&
                 field_is("pen.update"));
    AOAHID_CHECK(digitizer_update(nodes.pen, &sample) == AOAHID_ERR_PARAM &&
                 field_is("digitizer.update"));
    AOAHID_CHECK(aoahid_battery_update(other, 1) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_keyboard_key(nullptr, 0x04U, 1U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_toggle_usage(nullptr, kMute[0], 1U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_touchpad_contact(nullptr, 1U, 1U, 0, 0, nullptr) == AOAHID_ERR_PARAM);
}

void test_values_outside_the_spec(const Nodes& nodes) {
    AOAHID_CHECK(aoahid_keyboard_key(nodes.keyboard, 0x04U, 2U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_mouse_button(nodes.mouse, 0U, 1U) == AOAHID_ERR_PARAM &&
                 field_is("mouse.button"));
    AOAHID_CHECK(aoahid_mouse_button(nodes.mouse, 6U, 1U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_mouse_button(nodes.mouse, 1U, 2U) == AOAHID_ERR_PARAM);
    // Pan is absent from this Spec; wheel is present.
    AOAHID_CHECK(aoahid_mouse_scroll(nodes.mouse, 0, 1) == AOAHID_ERR_UNSUPPORTED &&
                 field_is("mouse.scroll"));
    AOAHID_CHECK(aoahid_mouse_scroll(nodes.mouse, 0, 0) == AOAHID_OK);
    AOAHID_CHECK(aoahid_toggle_usage(nodes.toggle, 0xE9U, 1U) == AOAHID_ERR_PARAM &&
                 field_is("toggle.usage"));
    AOAHID_CHECK(aoahid_toggle_usage(nodes.toggle, kMute[0], 2U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_gamepad_button(nodes.gamepad, 0U, 1U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_gamepad_button(nodes.gamepad, 13U, 1U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_gamepad_button(nodes.gamepad, 1U, 2U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_gamepad_axis(nodes.gamepad, 4U, 0) == AOAHID_ERR_PARAM &&
                 field_is("gamepad.axis"));
    AOAHID_CHECK(aoahid_gamepad_axis(nodes.gamepad, 0U, 128) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_gamepad_axis(nodes.gamepad, 0U, -128) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_gamepad_dpad(nodes.gamepad, 2U, 0U, 0U, 0U) == AOAHID_ERR_PARAM &&
                 field_is("gamepad.dpad"));
    AOAHID_CHECK(aoahid_gamepad_dpad(nodes.buttons_pad, 1U, 0U, 0U, 0U) == AOAHID_ERR_PARAM);

    // Touch: the flag, the coordinates, a lift of an inactive ID, and the slot count.
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchscreen, 1U, 2U, 0, 0, nullptr) ==
                     AOAHID_ERR_PARAM &&
                 field_is("touch"));
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchscreen, 1U, 1U, 40000, 0, nullptr) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchscreen, 1U, 0U, 0, 0, nullptr) ==
                     AOAHID_ERR_PARAM &&
                 field_is("touchscreen.contact_id"));
    AOAHID_CHECK(aoahid_touchpad_contact(nodes.touchpad, 1U, 0U, 0, 0, nullptr) ==
                     AOAHID_ERR_PARAM &&
                 field_is("touchpad.contact_id"));
    const aoahid_touchscreen_extra extra{0, 50, 60, 0};
    const aoahid_touchscreen_extra too_wide{0, 101, 60, 0};
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchscreen, 1U, 1U, 5, 5, &too_wide) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchscreen, 1U, 1U, 5, 5, &extra) == AOAHID_OK);
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchscreen, 2U, 1U, 6, 6, &extra) == AOAHID_OK);
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchscreen, 3U, 1U, 7, 7, nullptr) ==
                     AOAHID_ERR_OVERFLOW &&
                 field_is("touchscreen.maximum_contacts"));
    // A move of a contact already down is accepted; a lift outside the range is not, and a
    // lift before the press was carried by a report is pending.
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchscreen, 1U, 1U, 8, 8, &extra) == AOAHID_OK);
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchscreen, 1U, 0U, 40000, 8, nullptr) ==
                     AOAHID_ERR_PARAM &&
                 field_is("touch"));
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchscreen, 1U, 0U, 8, 8, nullptr) ==
                 AOAHID_ERR_BUSY);
    AOAHID_CHECK(aoahid_touchpad_button(nodes.touchpad, 0U, 1U) == AOAHID_ERR_PARAM &&
                 field_is("touchpad.button"));
    AOAHID_CHECK(aoahid_touchpad_button(nodes.touchpad, 2U, 1U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_touchpad_button(nodes.touchpad, 1U, 2U) == AOAHID_ERR_PARAM);
    const aoahid_touchpad_extra pad_extra{0, 0, 0, 0};
    AOAHID_CHECK(aoahid_touchpad_contact(nodes.touchpad, 1U, 1U, 5, 5, &pad_extra) == AOAHID_OK);

    // Pen samples: null, flags, a tip out of range, the eraser, buttons and every value.
    AOAHID_CHECK(aoahid_pen_update(nodes.pen, nullptr) == AOAHID_ERR_PARAM &&
                 field_is("pen.sample"));
    AOAHID_CHECK(digitizer_update(nodes.digitizer, nullptr) == AOAHID_ERR_PARAM &&
                 field_is("digitizer.sample"));
    using Edit = void (*)(aoahid_pen_sample*);
    static constexpr std::array<Edit, 13U> bad{
        [](aoahid_pen_sample* s) { s->in_range = 2U; },
        [](aoahid_pen_sample* s) { s->tip = 2U; },
        [](aoahid_pen_sample* s) { s->eraser = 2U; },
        [](aoahid_pen_sample* s) { s->in_range = 0U; },
        [](aoahid_pen_sample* s) { s->eraser = 1U; },
        [](aoahid_pen_sample* s) { s->barrel_buttons = 2U; },
        [](aoahid_pen_sample* s) { s->x = -1; },
        [](aoahid_pen_sample* s) { s->y = 40000; },
        [](aoahid_pen_sample* s) { s->pressure = 5000; },
        [](aoahid_pen_sample* s) { s->tilt_x = 91; },
        [](aoahid_pen_sample* s) { s->tilt_y = -91; },
        [](aoahid_pen_sample* s) { s->twist = 360; },
        [](aoahid_pen_sample* s) { s->twist = -1; },
    };
    for (const Edit edit : bad) {
        aoahid_pen_sample sample = touching(10);
        edit(&sample);
        AOAHID_CHECK(aoahid_pen_update(nodes.pen, &sample) == AOAHID_ERR_PARAM);
    }
    // Without hover, in range means touching; without tilt and twist, those values are zero.
    aoahid_pen_sample hover = touching(10);
    hover.tip = 0U;
    hover.pressure = 0;
    AOAHID_CHECK(digitizer_update(nodes.digitizer, &hover) == AOAHID_ERR_PARAM);
    aoahid_pen_sample tilted = touching(10);
    tilted.tilt_x = 1;
    AOAHID_CHECK(digitizer_update(nodes.digitizer, &tilted) == AOAHID_ERR_PARAM);
    aoahid_pen_sample twisted = touching(10);
    twisted.twist = 1;
    AOAHID_CHECK(digitizer_update(nodes.digitizer, &twisted) == AOAHID_ERR_PARAM);
    aoahid_pen_sample full = touching(10);
    full.tilt_x = -10;
    full.tilt_y = 20;
    full.twist = 90;
    full.barrel_buttons = 1U;
    AOAHID_CHECK(aoahid_pen_update(nodes.pen, &full) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(nodes.pen, kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(digitizer_update(nodes.digitizer, &full) == AOAHID_ERR_PARAM);
    const aoahid_pen_sample plain = touching(10);
    AOAHID_CHECK(digitizer_update(nodes.digitizer, &plain) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(nodes.digitizer, kSendTimeoutMs) == AOAHID_OK);
}

// A press, then a release before any report carried the press, is refused with BUSY: the
// target would never see the press.
void test_transitions_wait_for_their_report(const Nodes& nodes) {
    AOAHID_CHECK(aoahid_keyboard_key(nodes.keyboard, 0xE1U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_keyboard_key(nodes.keyboard, 0xE1U, 0U) == AOAHID_ERR_BUSY &&
                 field_is("keyboard.key"));
    AOAHID_CHECK(aoahid_mouse_button(nodes.mouse, 2U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_mouse_button(nodes.mouse, 2U, 0U) == AOAHID_ERR_BUSY &&
                 field_is("mouse.button"));
    AOAHID_CHECK(aoahid_toggle_usage(nodes.toggle, kMute[0], 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_toggle_usage(nodes.toggle, kMute[0], 0U) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(aoahid_gamepad_button(nodes.gamepad, 3U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_gamepad_button(nodes.gamepad, 3U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_gamepad_button(nodes.gamepad, 3U, 0U) == AOAHID_ERR_BUSY &&
                 field_is("gamepad.button"));
    AOAHID_CHECK(aoahid_gamepad_axis(nodes.gamepad, 1U, -100) == AOAHID_OK);
    AOAHID_CHECK(aoahid_touchpad_button(nodes.touchpad, 1U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_touchpad_button(nodes.touchpad, 1U, 0U) == AOAHID_ERR_BUSY);
    // Each report carries its transitions; then the release is accepted.
    for (aoahid_node* node : {nodes.keyboard, nodes.mouse, nodes.toggle, nodes.gamepad,
                              nodes.touchpad, nodes.touchscreen}) {
        AOAHID_CHECK(aoahid_node_submit_blocking(node, kSendTimeoutMs) == AOAHID_OK);
    }
    AOAHID_CHECK(aoahid_keyboard_key(nodes.keyboard, 0xE1U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_mouse_button(nodes.mouse, 2U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_toggle_usage(nodes.toggle, kMute[0], 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_gamepad_button(nodes.gamepad, 3U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_touchpad_button(nodes.touchpad, 1U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchscreen, 1U, 0U, 8, 8, nullptr) == AOAHID_OK);
    // A second press of a Contact ID whose lift is not sent yet is BUSY.
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchscreen, 1U, 1U, 8, 8, nullptr) ==
                     AOAHID_ERR_BUSY &&
                 field_is("touchscreen.contact_id"));
    for (aoahid_node* node : {nodes.keyboard, nodes.mouse, nodes.toggle, nodes.gamepad,
                              nodes.touchpad, nodes.touchscreen}) {
        AOAHID_CHECK(aoahid_node_submit_blocking(node, kSendTimeoutMs) == AOAHID_OK);
    }
}

// While a report is in flight the Toggle and touch state does not change under it.
void test_in_flight_state_is_frozen(const Nodes& nodes) {
    AOAHID_CHECK(aoahid_toggle_usage(nodes.toggle, kMute[0], 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchscreen, 1U, 1U, 1, 1, nullptr) == AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, -1, 1000000U);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, -1, 1000000U);
    AOAHID_CHECK(aoahid_node_submit(nodes.toggle, kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit(nodes.touchscreen, kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_toggle_usage(nodes.toggle, kMute[0], 0U) == AOAHID_ERR_BUSY &&
                 field_is("toggle"));
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchscreen, 1U, 1U, 2, 2, nullptr) ==
                     AOAHID_ERR_BUSY &&
                 field_is("touch"));
    aoahid_fake_libusb_make_pending_transfers_ready();
}

// A phone that is gone freezes the state: every call reports NO_DEVICE and the Node's ID.
void test_gone_phone_freezes_state(const Nodes& nodes, const sim::Session& session) {
    aoahid_fake_libusb_unplug(session.phone.index);
    // The library learns of it from the next transfer.
    const std::uint16_t keyboard_id = aoahid_node_id(nodes.keyboard);
    AOAHID_CHECK(aoahid_keyboard_key(nodes.keyboard, 0x05U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit(nodes.keyboard, kSendTimeoutMs) == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_keyboard_key(nodes.keyboard, 0x04U, 1U) == AOAHID_ERR_NO_DEVICE &&
                 aoahid_last_error()->node_id == keyboard_id);
    AOAHID_CHECK(aoahid_toggle_usage(nodes.toggle, kMute[0], 1U) == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_touchscreen_contact(nodes.touchscreen, 2U, 1U, 1, 1, nullptr) ==
                 AOAHID_ERR_NO_DEVICE);
    const aoahid_pen_sample sample = touching(1);
    AOAHID_CHECK(aoahid_pen_update(nodes.pen, &sample) == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_device_latched_error(session.device) == AOAHID_ERR_NO_DEVICE &&
                 field_is("device.latched_error"));
}

void test_report_state_suite() {
    aoahid_fake_libusb_reset();
    sim::Session session{};
    AOAHID_CHECK(session.open(nullptr));
    const Nodes nodes = open_every_profile(session.device);
    test_wrong_profile_is_refused(nodes);
    test_values_outside_the_spec(nodes);
    test_transitions_wait_for_their_report(nodes);
    test_in_flight_state_is_frozen(nodes);
    AOAHID_CHECK(aoahid_context_poll(session.context, 1U) == AOAHID_OK);
    test_gone_phone_freezes_state(nodes, session);
    AOAHID_CHECK(session.close() != AOAHID_CLOSE_PENDING);
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 0U);
    aoahid_fake_libusb_reset();
}

} // namespace

void test_report_state() { test_report_state_suite(); }
