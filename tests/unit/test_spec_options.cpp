// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Every Spec factory refuses each invalid option on its own: one edit to a
 * valid option structure, one expected result and the field the error names.
 * Nothing is inferred, so a value the caller left out is never filled in. */
#include "aoahid.h"
#include "api/internal.hpp"
#include "test.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>

namespace {

constexpr aoahid_physical_properties kMillimetres{1U, 0, 100, -1, 0x11U};

bool field_is(const char* field) {
    const aoahid_error_detail* detail = aoahid_last_error();
    return field == nullptr || (detail->field != nullptr && std::strcmp(detail->field, field) == 0);
}

template <typename Options> struct Case {
    std::function<void(Options*)> edit;
    aoahid_result expected;
    const char* field;
};

// Applies every case to a fresh copy of base; a case that is accepted must make a Spec.
template <typename Options, typename Create>
void check_cases(const Options& base, Create create,
                 const std::initializer_list<Case<Options>> cases) {
    aoahid_spec* valid = nullptr;
    AOAHID_CHECK(create(&base, &valid) == AOAHID_OK && valid != nullptr);
    aoahid_spec_release(valid);
    std::size_t index = 0U;
    for (const Case<Options>& entry : cases) {
        Options options = base;
        entry.edit(&options);
        aoahid_spec* spec = reinterpret_cast<aoahid_spec*>(&options);
        const aoahid_result result = create(&options, &spec);
        const bool matched = result == entry.expected &&
                             aoahid_last_error()->code == entry.expected && field_is(entry.field) &&
                             (result == AOAHID_OK) == (spec != nullptr);
        if (!matched) {
            std::fprintf(stderr, "case %zu: %s (%s)\n", index, aoahid_result_name(result),
                         aoahid_last_error()->field == nullptr ? "-" : aoahid_last_error()->field);
        }
        AOAHID_CHECK(matched);
        aoahid_spec_release(spec);
        ++index;
    }
    // A null structure, a wrong struct_size and a null output are refused before anything else.
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(create(nullptr, &spec) == AOAHID_ERR_PARAM && spec == nullptr);
    Options wrong = base;
    wrong.struct_size = 0U;
    AOAHID_CHECK(create(&wrong, &spec) == AOAHID_ERR_PARAM && spec == nullptr);
    AOAHID_CHECK(create(&base, nullptr) == AOAHID_ERR_PARAM);
}

aoahid_battery_report battery(const std::uint8_t report_id) {
    aoahid_battery_report value{};
    value.report_id = aoahid_report_id_option{1U, report_id, {}};
    value.strength = aoahid_integer_field_make(0, 100, 8U);
    return value;
}

// The battery rules are shared by every generated profile; the mouse carries them here.
void test_mouse_options() {
    aoahid_mouse_options base;
    aoahid_mouse_options_init(&base);
    using O = aoahid_mouse_options;
    check_cases<O>(
        base, aoahid_spec_create_mouse,
        {
            {[](O* o) { o->reserved = 1U; }, AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) { o->report_id.enabled = 2U; }, AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) {
                 o->report_id = {1U, 0U, {}};
             },
             AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) {
                 o->report_id = {0U, 3U, {}};
             },
             AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) {
                 o->report_id = {1U, 3U, {1U, 0U, 0U}};
             },
             AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) { o->x.bit_width = 0U; }, AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) { o->x.bit_width = 33U; }, AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) { o->x = aoahid_integer_field_make(0, 127, 8U); }, AOAHID_ERR_PARAM,
             "mouse_options"},
            {[](O* o) { o->y = aoahid_integer_field_make(-127, 0, 8U); }, AOAHID_ERR_PARAM,
             "mouse_options"},
            {[](O* o) { o->x = aoahid_integer_field_make(-200, 200, 8U); }, AOAHID_ERR_PARAM,
             "mouse_options"},
            {[](O* o) { o->button_count = 0U; }, AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) { o->button_count = 65536U; }, AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) { o->enable_wheel = 2U; }, AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) { o->enable_pan = 2U; }, AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) { o->wheel.bit_width = 0U; }, AOAHID_ERR_UNSET_FIELD, "mouse.wheel"},
            {[](O* o) { o->pan.bit_width = 40U; }, AOAHID_ERR_PARAM, "mouse.pan"},
            {[](O* o) { o->enable_wheel = 0U; }, AOAHID_ERR_PARAM, "mouse.disabled_fields"},
            {[](O* o) { o->enable_pan = 0U; }, AOAHID_ERR_PARAM, "mouse.disabled_fields"},
            {[](O* o) {
                 o->enable_wheel = 0U;
                 o->wheel = {};
                 o->enable_pan = 0U;
                 o->pan = {};
             },
             AOAHID_OK, nullptr},
            {[](O* o) { o->wheel = aoahid_integer_field_make(0, 127, 8U); }, AOAHID_ERR_PARAM,
             "mouse.scroll_range"},
            {[](O* o) { o->pan = aoahid_integer_field_make(-127, 0, 8U); }, AOAHID_ERR_PARAM,
             "mouse.scroll_range"},
            // Physical properties: a flag of exactly zero or one, canonical zero when disabled,
            // and ordered extents with a defined unit when enabled.
            {[](O* o) { o->x.physical.enabled = 2U; }, AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) { o->x.physical.unit = 0x11U; }, AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) { o->x.physical = kMillimetres; }, AOAHID_OK, nullptr},
            {[](O* o) {
                 o->x.physical = kMillimetres;
                 o->x.physical.maximum = 0;
             },
             AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) {
                 o->x.physical = kMillimetres;
                 o->x.physical.unit_exponent = -9;
             },
             AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) {
                 o->x.physical = kMillimetres;
                 o->x.physical.unit_exponent = 8;
             },
             AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) {
                 o->x.physical = kMillimetres;
                 o->x.physical.unit = 0U;
             },
             AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) {
                 o->x.physical = kMillimetres;
                 o->x.physical.unit = 0x15U; // system nibble 5 is reserved
             },
             AOAHID_ERR_PARAM, "mouse_options"},
            {[](O* o) {
                 o->x.physical = kMillimetres;
                 o->x.physical.unit = 0x10000011U;
             },
             AOAHID_ERR_PARAM, "mouse_options"},
            // The battery: a flag, a zero block while disabled, two distinct Report IDs and an
            // ordered nonnegative Strength range.
            {[](O* o) { o->enable_battery = 2U; }, AOAHID_ERR_PARAM, "mouse.enable_battery"},
            {[](O* o) { o->battery.strength.bit_width = 8U; }, AOAHID_ERR_PARAM, "mouse.battery"},
            {[](O* o) { o->battery.report_id.reserved8[2] = 1U; }, AOAHID_ERR_PARAM,
             "mouse.battery"},
            {[](O* o) {
                 o->enable_battery = 1U;
                 o->report_id = {1U, 1U, {}};
                 o->battery = battery(2U);
             },
             AOAHID_OK, nullptr},
            {[](O* o) {
                 o->enable_battery = 1U;
                 o->battery = battery(2U);
             },
             AOAHID_ERR_PARAM, "mouse.battery.report_id"},
            {[](O* o) {
                 o->enable_battery = 1U;
                 o->report_id = {1U, 2U, {}};
                 o->battery = battery(2U);
             },
             AOAHID_ERR_PARAM, "mouse.battery.report_id"},
            {[](O* o) {
                 o->enable_battery = 1U;
                 o->report_id = {1U, 1U, {}};
                 o->battery = battery(2U);
                 o->battery.report_id.enabled = 0U;
             },
             AOAHID_ERR_PARAM, "mouse.battery.report_id"},
            {[](O* o) {
                 o->enable_battery = 1U;
                 o->report_id = {1U, 1U, {}};
                 o->battery = battery(0U);
             },
             AOAHID_ERR_PARAM, "mouse.battery.report_id"},
            {[](O* o) {
                 o->enable_battery = 1U;
                 o->report_id = {1U, 1U, {}};
                 o->battery = battery(2U);
                 o->battery.strength = aoahid_integer_field_make(-1, 100, 8U);
             },
             AOAHID_ERR_PARAM, "mouse.battery.strength"},
            {[](O* o) {
                 o->enable_battery = 1U;
                 o->report_id = {1U, 1U, {}};
                 o->battery = battery(2U);
                 o->battery.strength = aoahid_integer_field_make(5, 5, 8U);
             },
             AOAHID_ERR_PARAM, "mouse.battery.strength"},
            {[](O* o) {
                 o->enable_battery = 1U;
                 o->report_id = {1U, 1U, {}};
                 o->battery = battery(2U);
                 o->battery.strength.bit_width = 0U;
             },
             AOAHID_ERR_PARAM, "mouse.battery.strength"},
        });
}

void test_keyboard_options() {
    aoahid_keyboard_options base;
    aoahid_keyboard_options_init(&base);
    using O = aoahid_keyboard_options;
    check_cases<O>(
        base, aoahid_spec_create_keyboard,
        {
            {[](O* o) { o->reserved = 1U; }, AOAHID_ERR_PARAM, "keyboard.reserved"},
            {[](O* o) {
                 o->report_id = {1U, 0U, {}};
             },
             AOAHID_ERR_PARAM, "keyboard.reserved"},
            {[](O* o) { o->usage_minimum = 0x03U; }, AOAHID_ERR_PARAM, "keyboard.usage_range"},
            {[](O* o) { o->usage_maximum = 0x03U; }, AOAHID_ERR_PARAM, "keyboard.usage_range"},
            {[](O* o) { o->usage_maximum = 0xE0U; }, AOAHID_ERR_PARAM, "keyboard.usage_range"},
            {[](O* o) {
                 o->usage_minimum = 0xE8U;
                 o->usage_maximum = 0xE9U;
             },
             AOAHID_OK, nullptr},
            {[](O* o) { o->enable_battery = 2U; }, AOAHID_ERR_PARAM, "keyboard.enable_battery"},
        });
}

constexpr std::array<std::uint16_t, 2U> kVolume{0xE9U, 0xEAU};
constexpr std::array<aoahid_usage_semantic, 2U> kRetrigger{AOAHID_USAGE_RETRIGGER,
                                                           AOAHID_USAGE_RETRIGGER};
constexpr std::array<const char*, 2U> kKeyEvents{"EV_KEY", "EV_KEY"};
constexpr std::array<const char*, 2U> kVolumeCodes{"KEY_VOLUMEUP", "KEY_VOLUMEDOWN"};

void test_toggle_options() {
    aoahid_toggle_options base;
    aoahid_toggle_options_init(&base);
    base.allowed_usages = kVolume.data();
    base.allowed_usage_count = kVolume.size();
    base.usage_semantics = kRetrigger.data();
    base.expected_linux_event_types = kKeyEvents.data();
    base.expected_linux_codes = kVolumeCodes.data();
    using O = aoahid_toggle_options;
    static constexpr std::array<std::uint16_t, 2U> duplicate{0xE9U, 0xE9U};
    static constexpr std::array<std::uint16_t, 2U> with_zero{0xE9U, 0U};
    static constexpr std::array<std::uint16_t, 2U> with_application{0xE9U, 0x01U};
    static constexpr std::array<const char*, 2U> empty_code{"KEY_VOLUMEUP", ""};
    static constexpr std::array<const char*, 2U> null_code{"KEY_VOLUMEUP", nullptr};
    static constexpr std::array<aoahid_usage_semantic, 2U> linear{AOAHID_USAGE_RETRIGGER,
                                                                  AOAHID_USAGE_LINEAR};
    static constexpr std::array<aoahid_usage_semantic, 2U> undefined{
        AOAHID_USAGE_RETRIGGER, static_cast<aoahid_usage_semantic>(99)};
    static constexpr std::array<std::uint16_t, 2U> camera{0x20U, 0x21U};
    static constexpr std::array<std::uint16_t, 2U> camera_other{0x20U, 0x22U};
    static constexpr std::array<aoahid_usage_semantic, 2U> one_shot{AOAHID_USAGE_ONE_SHOT,
                                                                    AOAHID_USAGE_ONE_SHOT};
    check_cases<O>(
        base, aoahid_spec_create_toggle,
        {
            {[](O* o) { o->reserved = 1U; }, AOAHID_ERR_PARAM, "toggle.reserved"},
            {[](O* o) { o->reserved16 = 1U; }, AOAHID_ERR_PARAM, "toggle.reserved"},
            {[](O* o) {
                 o->report_id = {1U, 0U, {}};
             },
             AOAHID_ERR_PARAM, "toggle.reserved"},
            {[](O* o) { o->application_page = 0U; }, AOAHID_ERR_UNSET_FIELD, "toggle.application"},
            {[](O* o) { o->application_usage = 0U; }, AOAHID_ERR_UNSET_FIELD, "toggle.application"},
            {[](O* o) { o->field_page = 0U; }, AOAHID_ERR_UNSET_FIELD, "toggle.application"},
            {[](O* o) { o->allowed_usages = nullptr; }, AOAHID_ERR_UNSET_FIELD,
             "toggle.allowed_usages"},
            {[](O* o) { o->allowed_usage_count = 0U; }, AOAHID_ERR_UNSET_FIELD,
             "toggle.allowed_usages"},
            {[](O* o) { o->usage_semantics = nullptr; }, AOAHID_ERR_UNSET_FIELD,
             "toggle.usage_semantics"},
            {[](O* o) { o->expected_linux_event_types = nullptr; }, AOAHID_ERR_UNSET_FIELD,
             "toggle.expected_linux_events"},
            {[](O* o) { o->expected_linux_codes = nullptr; }, AOAHID_ERR_UNSET_FIELD,
             "toggle.expected_linux_events"},
            {[](O* o) { o->allowed_usage_count = 65536U; }, AOAHID_ERR_OVERFLOW,
             "toggle.allowed_usage_count"},
            {[](O* o) { o->allowed_usages = with_zero.data(); }, AOAHID_ERR_PARAM,
             "toggle.allowed_usages"},
            {[](O* o) { o->expected_linux_codes = empty_code.data(); }, AOAHID_ERR_PARAM,
             "toggle.allowed_usages"},
            {[](O* o) { o->expected_linux_codes = null_code.data(); }, AOAHID_ERR_PARAM,
             "toggle.allowed_usages"},
            {[](O* o) { o->expected_linux_event_types = empty_code.data(); }, AOAHID_ERR_PARAM,
             "toggle.allowed_usages"},
            {[](O* o) { o->allowed_usages = duplicate.data(); }, AOAHID_ERR_PARAM,
             "toggle.allowed_usages"},
            {[](O* o) { o->allowed_usages = with_application.data(); }, AOAHID_ERR_PARAM,
             "toggle.allowed_usages"},
            {[](O* o) { o->usage_semantics = linear.data(); }, AOAHID_ERR_UNSUPPORTED,
             "toggle.usage_semantics"},
            {[](O* o) { o->usage_semantics = undefined.data(); }, AOAHID_ERR_PARAM,
             "toggle.usage_semantics"},
            // Camera Control has two Usages, both One Shot.
            {[](O* o) {
                 o->field_page = 0x90U;
                 o->allowed_usages = camera.data();
                 o->usage_semantics = one_shot.data();
             },
             AOAHID_OK, nullptr},
            {[](O* o) {
                 o->field_page = 0x90U;
                 o->allowed_usages = camera_other.data();
                 o->usage_semantics = one_shot.data();
             },
             AOAHID_ERR_PARAM, "toggle.allowed_usages"},
            {[](O* o) {
                 o->field_page = 0x90U;
                 o->allowed_usages = camera.data();
             },
             AOAHID_ERR_UNSUPPORTED, "toggle.usage_semantics"},
            {[](O* o) { o->enable_battery = 1U; }, AOAHID_ERR_PARAM, "toggle.battery.report_id"},
        });
}

// Axes the gamepad tests edit in place; each case starts from a copy.
struct Axes {
    std::array<aoahid_gamepad_axis_option, 4U> values{{
        {AOAHID_AXIS_X, 0x01U, 0x30U, {-127, 127, 8U, {}}, 0, "ABS_X", "AXIS_X"},
        {AOAHID_AXIS_Y, 0x01U, 0x31U, {-127, 127, 8U, {}}, 0, "ABS_Y", "AXIS_Y"},
        {AOAHID_AXIS_Z, 0x01U, 0x32U, {-127, 127, 8U, {}}, 0, "ABS_Z", "AXIS_Z"},
        {AOAHID_AXIS_RZ, 0x01U, 0x35U, {-127, 127, 8U, {}}, 0, "ABS_RZ", "AXIS_RZ"},
    }};
};

Axes g_axes{};

void test_gamepad_options() {
    aoahid_gamepad_options base;
    aoahid_gamepad_options_init(&base);
    base.axes = g_axes.values.data();
    using O = aoahid_gamepad_options;
    const auto with_axis = [](const std::size_t index,
                              const std::function<void(aoahid_gamepad_axis_option*)>& edit) {
        return [index, edit](O* o) {
            g_axes = Axes{};
            edit(&g_axes.values[index]);
            o->axes = g_axes.values.data();
        };
    };
    const auto role = [](const aoahid_axis_role value, const std::uint16_t page,
                         const std::uint16_t usage) {
        return [=](aoahid_gamepad_axis_option* axis) {
            axis->role = value;
            axis->usage_page = page;
            axis->usage = usage;
        };
    };
    check_cases<O>(
        base,
        [](const O* options, aoahid_spec** out) {
            const aoahid_result result = aoahid_spec_create_gamepad(options, out);
            g_axes = Axes{};
            return result;
        },
        {
            {[](O* o) { o->reserved = 1U; }, AOAHID_ERR_PARAM, "gamepad"},
            {[](O* o) { o->axes = nullptr; }, AOAHID_ERR_PARAM, "gamepad"},
            {[](O* o) { o->axis_count = 1U; }, AOAHID_ERR_PARAM, "gamepad"},
            {[](O* o) { o->button_count = 0U; }, AOAHID_ERR_PARAM, "gamepad"},
            {[](O* o) { o->button_count = 65536U; }, AOAHID_ERR_PARAM, "gamepad"},
            {[](O* o) { o->button_usage_minimum = 0U; }, AOAHID_ERR_PARAM, "gamepad"},
            {[](O* o) { o->dpad_representation = static_cast<aoahid_dpad_representation>(0); },
             AOAHID_ERR_PARAM, "gamepad"},
            {[](O* o) { o->axis_count = 65536U; }, AOAHID_ERR_OVERFLOW, "gamepad.axis_count"},
            {[](O* o) { o->button_usage_minimum = 65535U; }, AOAHID_ERR_OVERFLOW,
             "gamepad.button_usage_minimum"},
            {[](O* o) {
                 o->dpad_representation = AOAHID_DPAD_BUTTONS;
                 o->hat_logical_maximum = 0;
                 o->hat_bit_width = 0U;
                 o->button_count = 65533U;
             },
             AOAHID_ERR_OVERFLOW, "gamepad.button_count"},
            {with_axis(2U, role(static_cast<aoahid_axis_role>(0), 0x01U, 0x32U)), AOAHID_ERR_PARAM,
             "gamepad.axes.role"},
            {with_axis(2U, [](aoahid_gamepad_axis_option* axis) { axis->usage = 0x33U; }),
             AOAHID_ERR_PARAM, "gamepad.axes.role"},
            {with_axis(2U, role(AOAHID_AXIS_X, 0x01U, 0x30U)), AOAHID_ERR_PARAM,
             "gamepad.axes.role"},
            {with_axis(2U, role(AOAHID_AXIS_RX, 0x01U, 0x33U)), AOAHID_OK, nullptr},
            {with_axis(2U, role(AOAHID_AXIS_RY, 0x01U, 0x34U)), AOAHID_OK, nullptr},
            {with_axis(2U, role(AOAHID_AXIS_SLIDER, 0x01U, 0x36U)), AOAHID_OK, nullptr},
            {with_axis(2U, role(AOAHID_AXIS_DIAL, 0x01U, 0x37U)), AOAHID_OK, nullptr},
            {with_axis(2U, role(AOAHID_AXIS_WHEEL, 0x01U, 0x38U)), AOAHID_OK, nullptr},
            {with_axis(2U, role(AOAHID_AXIS_SIMULATION_STEERING, 0x02U, 0xC8U)), AOAHID_OK,
             nullptr},
            {with_axis(2U, role(AOAHID_AXIS_SIMULATION_RUDDER, 0x02U, 0xBAU)), AOAHID_OK, nullptr},
            {with_axis(2U,
                       [](aoahid_gamepad_axis_option* axis) {
                           axis->role = AOAHID_AXIS_SIMULATION_ACCELERATOR;
                           axis->usage_page = 0x02U;
                           axis->usage = 0xC4U;
                           axis->value = {0, 255, 8U, {}};
                       }),
             AOAHID_OK, nullptr},
            {with_axis(2U,
                       [](aoahid_gamepad_axis_option* axis) {
                           axis->role = AOAHID_AXIS_SIMULATION_BRAKE;
                           axis->usage_page = 0x02U;
                           axis->usage = 0xC5U;
                           axis->value = {0, 255, 8U, {}};
                       }),
             AOAHID_OK, nullptr},
            {with_axis(2U,
                       [](aoahid_gamepad_axis_option* axis) {
                           axis->role = AOAHID_AXIS_SIMULATION_THROTTLE;
                           axis->usage_page = 0x02U;
                           axis->usage = 0xBBU;
                       }),
             AOAHID_ERR_PARAM, "gamepad.axes.value"},
            {with_axis(2U,
                       [](aoahid_gamepad_axis_option* axis) {
                           axis->role = AOAHID_AXIS_SIMULATION_STEERING;
                           axis->usage_page = 0x02U;
                           axis->usage = 0xC8U;
                           axis->value = {0, 255, 8U, {}};
                       }),
             AOAHID_ERR_PARAM, "gamepad.axes.value"},
            {with_axis(2U, [](aoahid_gamepad_axis_option* axis) { axis->value.bit_width = 0U; }),
             AOAHID_ERR_UNSET_FIELD, "gamepad.axes"},
            {with_axis(2U, [](aoahid_gamepad_axis_option* axis) { axis->neutral_value = 200; }),
             AOAHID_ERR_UNSET_FIELD, "gamepad.axes"},
            {with_axis(2U, [](aoahid_gamepad_axis_option* axis) { axis->neutral_value = -200; }),
             AOAHID_ERR_UNSET_FIELD, "gamepad.axes"},
            {with_axis(2U,
                       [](aoahid_gamepad_axis_option* axis) { axis->expected_linux_code = ""; }),
             AOAHID_ERR_UNSET_FIELD, "gamepad.axes"},
            {with_axis(
                 2U,
                 [](aoahid_gamepad_axis_option* axis) { axis->expected_android_axis = nullptr; }),
             AOAHID_ERR_UNSET_FIELD, "gamepad.axes"},
            {with_axis(
                 2U, [](aoahid_gamepad_axis_option* axis) { axis->expected_linux_code = nullptr; }),
             AOAHID_ERR_UNSET_FIELD, "gamepad.axes"},
            {with_axis(2U,
                       [](aoahid_gamepad_axis_option* axis) { axis->expected_android_axis = ""; }),
             AOAHID_ERR_UNSET_FIELD, "gamepad.axes"},
            // X and Y must both be declared.
            {with_axis(0U, role(AOAHID_AXIS_RX, 0x01U, 0x33U)), AOAHID_ERR_PARAM, "gamepad.axes"},
            {with_axis(1U, role(AOAHID_AXIS_RY, 0x01U, 0x34U)), AOAHID_ERR_PARAM, "gamepad.axes"},
            {[](O* o) { o->hat_logical_maximum = 6; }, AOAHID_ERR_PARAM, "gamepad.hat"},
            {[](O* o) { o->hat_logical_minimum = 1; }, AOAHID_ERR_PARAM, "gamepad.hat"},
            {[](O* o) { o->hat_bit_width = 3U; }, AOAHID_ERR_PARAM, "gamepad.hat"},
            {[](O* o) { o->dpad_representation = AOAHID_DPAD_NONE; }, AOAHID_ERR_PARAM,
             "gamepad.hat"},
            {[](O* o) {
                 o->dpad_representation = AOAHID_DPAD_NONE;
                 o->hat_logical_maximum = 0;
                 o->hat_bit_width = 0U;
             },
             AOAHID_OK, nullptr},
            {[](O* o) {
                 o->dpad_representation = AOAHID_DPAD_BUTTONS;
                 o->hat_logical_maximum = 0;
                 o->hat_bit_width = 4U;
             },
             AOAHID_ERR_PARAM, "gamepad.hat"},
            {[](O* o) {
                 o->dpad_representation = AOAHID_DPAD_BUTTONS;
                 o->hat_logical_minimum = -1;
                 o->hat_logical_maximum = 0;
                 o->hat_bit_width = 0U;
             },
             AOAHID_ERR_PARAM, "gamepad.hat"},
            {[](O* o) { o->enable_battery = 1U; }, AOAHID_ERR_PARAM, "gamepad.battery.report_id"},
        });
    // Fewer than five buttons from Button 1 is not the Android A/B/X/Y layout.
    aoahid_gamepad_options few = base;
    few.button_count = 4U;
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_gamepad(&few, &spec) == AOAHID_OK);
    aoahid_capability_manifest manifest;
    aoahid_capability_manifest_init(&manifest);
    AOAHID_CHECK(aoahid_spec_manifest(spec, &manifest) == AOAHID_OK &&
                 manifest.android_status == AOAHID_ANDROID_CONDITIONAL);
    aoahid_spec_release(spec);
}

template <typename O> std::initializer_list<Case<O>> touch_cases(const char* prefix);

void test_touch_options() {
    aoahid_touchscreen_options base;
    aoahid_touchscreen_options_init(&base);
    using O = aoahid_touchscreen_options;
    const aoahid_integer_field pressure = aoahid_integer_field_make(0, 255, 8U);
    const aoahid_integer_field geometry = aoahid_integer_field_make(0, 4095, 12U);
    check_cases<O>(
        base, aoahid_spec_create_touchscreen,
        {
            {[](O* o) { o->reserved = 1U; }, AOAHID_ERR_PARAM, "touchscreen_options"},
            {[](O* o) {
                 o->report_id = {1U, 0U, {}};
             },
             AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->x = aoahid_integer_field_make(1, 100, 8U); }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->y = aoahid_integer_field_make(-100, -1, 8U); }, AOAHID_ERR_PARAM,
             nullptr},
            {[](O* o) { o->maximum_contacts = 0U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->maximum_contacts = 17U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->contacts_per_report = 0U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->contacts_per_report = 11U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->enable_pressure = 2U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->enable_width = 2U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->enable_height = 2U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->enable_azimuth = 2U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->enable_scan_time = 2U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->scan_time_unit_100us = 2U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->enable_contact_count_maximum_feature_declaration = 2U; },
             AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->enable_multi_packet_frames = 2U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->contact_id.bit_width = 0U; }, AOAHID_ERR_PARAM,
             "touchscreen.contact_fields"},
            {[](O* o) { o->contact_count.bit_width = 0U; }, AOAHID_ERR_PARAM,
             "touchscreen.contact_fields"},
            {[](O* o) { o->contact_count = aoahid_integer_field_make(1, 10, 4U); },
             AOAHID_ERR_PARAM, "touchscreen.contact_count"},
            {[](O* o) { o->contact_count = aoahid_integer_field_make(0, 9, 4U); }, AOAHID_ERR_PARAM,
             "touchscreen.contact_count"},
            {[](O* o) { o->contact_id = aoahid_integer_field_make(1, 15, 4U); }, AOAHID_ERR_PARAM,
             "touchscreen.contact_id"},
            {[](O* o) { o->contact_id = aoahid_integer_field_make(-8, -1, 4U); }, AOAHID_ERR_PARAM,
             "touchscreen.contact_id"},
            {[](O* o) { o->contact_id = aoahid_integer_field_make(0, 8, 4U); }, AOAHID_ERR_PARAM,
             "touchscreen.contact_id"},
            {[](O* o) { o->contacts_per_report = 5U; }, AOAHID_ERR_PARAM,
             "touchscreen.contacts_per_report"},
            {[](O* o) {
                 o->contacts_per_report = 5U;
                 o->enable_multi_packet_frames = 1U;
             },
             AOAHID_OK, nullptr},
            {[&](O* o) {
                 o->enable_pressure = 1U;
                 o->pressure = pressure;
             },
             AOAHID_OK, nullptr},
            {[](O* o) {
                 o->enable_pressure = 1U;
                 o->pressure = aoahid_integer_field_make(1, 255, 8U);
             },
             AOAHID_ERR_PARAM, "touchscreen.pressure"},
            {[](O* o) {
                 o->enable_pressure = 1U;
                 o->pressure = aoahid_integer_field_make(0, 0, 8U);
             },
             AOAHID_ERR_PARAM, "touchscreen.pressure"},
            {[](O* o) {
                 o->enable_pressure = 1U;
                 o->pressure = aoahid_integer_field_make(0, 255, 0U);
             },
             AOAHID_ERR_PARAM, "touchscreen.pressure"},
            {[&](O* o) { o->pressure = pressure; }, AOAHID_ERR_PARAM,
             "touchscreen.disabled_fields"},
            {[&](O* o) { o->width = geometry; }, AOAHID_ERR_PARAM, "touchscreen.disabled_fields"},
            {[&](O* o) { o->height = geometry; }, AOAHID_ERR_PARAM, "touchscreen.disabled_fields"},
            {[&](O* o) { o->azimuth = geometry; }, AOAHID_ERR_PARAM, "touchscreen.disabled_fields"},
            {[&](O* o) { o->scan_time = geometry; }, AOAHID_ERR_PARAM,
             "touchscreen.disabled_fields"},
            {[](O* o) { o->scan_time_unit_100us = 1U; }, AOAHID_ERR_PARAM,
             "touchscreen.disabled_fields"},
            {[&](O* o) {
                 o->enable_azimuth = 1U;
                 o->azimuth = geometry;
             },
             AOAHID_OK, nullptr},
            {[](O* o) {
                 o->enable_azimuth = 1U;
                 o->azimuth = aoahid_integer_field_make(-1, 359, 16U);
             },
             AOAHID_ERR_PARAM, "touchscreen.azimuth"},
            {[](O* o) {
                 o->enable_azimuth = 1U;
                 o->azimuth = aoahid_integer_field_make(0, 0, 16U);
             },
             AOAHID_ERR_PARAM, "touchscreen.azimuth"},
            {[](O* o) {
                 o->enable_azimuth = 1U;
                 o->azimuth = aoahid_integer_field_make(0, 359, 0U);
             },
             AOAHID_ERR_PARAM, "touchscreen.azimuth"},
            {[&](O* o) {
                 o->enable_width = 1U;
                 o->width = geometry;
                 o->enable_height = 1U;
                 o->height = geometry;
             },
             AOAHID_OK, nullptr},
            {[](O* o) {
                 o->enable_width = 1U;
                 o->width = aoahid_integer_field_make(0, 4095, 0U);
             },
             AOAHID_ERR_UNSET_FIELD, "touchscreen.width"},
            {[](O* o) {
                 o->enable_height = 1U;
                 o->height = aoahid_integer_field_make(0, 4095, 0U);
             },
             AOAHID_ERR_UNSET_FIELD, "touchscreen.height"},
            {[](O* o) {
                 o->enable_width = 1U;
                 o->width = aoahid_integer_field_make(1, 4095, 12U);
             },
             AOAHID_ERR_PARAM, "touchscreen.inactive_geometry"},
            {[](O* o) {
                 o->enable_height = 1U;
                 o->height = aoahid_integer_field_make(-10, -1, 8U);
             },
             AOAHID_ERR_PARAM, "touchscreen.inactive_geometry"},
            {[&](O* o) {
                 o->enable_width = 1U;
                 o->width = geometry;
                 o->width.physical = kMillimetres;
             },
             AOAHID_ERR_PARAM, "touchscreen.geometry_units"},
            {[&](O* o) {
                 o->enable_height = 1U;
                 o->height = geometry;
                 o->height.physical = kMillimetres;
             },
             AOAHID_ERR_PARAM, "touchscreen.geometry_units"},
            {[&](O* o) {
                 o->x.physical = kMillimetres;
                 o->enable_width = 1U;
                 o->width = geometry;
                 o->width.physical = kMillimetres;
                 o->width.physical.unit = 0x13U;
             },
             AOAHID_ERR_PARAM, "touchscreen.geometry_units"},
            {[](O* o) {
                 o->enable_scan_time = 1U;
                 o->scan_time = aoahid_integer_field_make(0, 65535, 16U);
                 o->scan_time_unit_100us = 1U;
             },
             AOAHID_OK, nullptr},
            {[](O* o) {
                 o->enable_scan_time = 1U;
                 o->scan_time = aoahid_integer_field_make(0, 65535, 16U);
             },
             AOAHID_ERR_PARAM, "touchscreen.scan_time"},
            {[](O* o) {
                 o->enable_scan_time = 1U;
                 o->scan_time = aoahid_integer_field_make(1, 65535, 16U);
                 o->scan_time_unit_100us = 1U;
             },
             AOAHID_ERR_PARAM, "touchscreen.scan_time"},
            {[](O* o) {
                 o->enable_scan_time = 1U;
                 o->scan_time = aoahid_integer_field_make(0, 0, 16U);
                 o->scan_time_unit_100us = 1U;
             },
             AOAHID_ERR_PARAM, "touchscreen.scan_time"},
            {[](O* o) {
                 o->enable_scan_time = 1U;
                 o->scan_time = aoahid_integer_field_make(0, 65535, 16U);
                 o->scan_time.physical = kMillimetres;
                 o->scan_time_unit_100us = 1U;
             },
             AOAHID_ERR_PARAM, "touchscreen.scan_time"},
            {[](O* o) {
                 o->enable_scan_time = 1U;
                 o->scan_time = aoahid_integer_field_make(0, 65535, 0U);
                 o->scan_time_unit_100us = 1U;
             },
             AOAHID_ERR_PARAM, "touchscreen.scan_time"},
            {[](O* o) { o->enable_contact_count_maximum_feature_declaration = 1U; }, AOAHID_OK,
             nullptr},
            {[](O* o) { o->enable_battery = 1U; }, AOAHID_ERR_PARAM,
             "touchscreen.battery.report_id"},
        });

    aoahid_touchpad_options pad;
    aoahid_touchpad_options_init(&pad);
    using P = aoahid_touchpad_options;
    check_cases<P>(
        pad, aoahid_spec_create_touchpad,
        {
            {[](P* o) { o->reserved = 1U; }, AOAHID_ERR_PARAM, "touchpad_options"},
            {[](P* o) { o->button_count = 65536U; }, AOAHID_ERR_OVERFLOW, "touchpad.button_count"},
            {[](P* o) { o->button_count = 0U; }, AOAHID_OK, nullptr},
            {[](P* o) { o->button_count = 3U; }, AOAHID_OK, nullptr},
            {[](P* o) { o->contact_count.bit_width = 0U; }, AOAHID_ERR_PARAM,
             "touchpad.contact_fields"},
            {[](P* o) { o->enable_battery = 1U; }, AOAHID_ERR_PARAM, "touchpad.battery.report_id"},
        });
}

void test_stylus_options() {
    aoahid_pen_options base;
    aoahid_pen_options_init(&base);
    using O = aoahid_pen_options;
    static constexpr std::array<std::uint16_t, 2U> both{0x44U, 0x5AU};
    static constexpr std::array<std::uint16_t, 2U> repeated{0x44U, 0x44U};
    static constexpr std::array<std::uint16_t, 1U> eraser{0x45U};
    const aoahid_integer_field tilt = aoahid_integer_field_make(-90, 90, 8U);
    check_cases<O>(
        base, aoahid_spec_create_pen,
        {
            {[](O* o) { o->reserved = 1U; }, AOAHID_ERR_PARAM, "pen_options"},
            {[](O* o) {
                 o->report_id = {1U, 0U, {}};
             },
             AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->x = aoahid_integer_field_make(1, 100, 8U); }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->y = aoahid_integer_field_make(-100, -1, 8U); }, AOAHID_ERR_PARAM,
             nullptr},
            {[](O* o) { o->enable_pressure = 2U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->enable_tilt = 2U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->enable_twist_target_specific = 2U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->enable_eraser = 2U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->enable_hover = 2U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->barrel_usage_count = 33U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->barrel_usages = nullptr; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->pressure = aoahid_integer_field_make(1, 4095, 12U); }, AOAHID_ERR_PARAM,
             nullptr},
            {[](O* o) { o->pressure = aoahid_integer_field_make(0, 0, 12U); }, AOAHID_ERR_PARAM,
             nullptr},
            {[](O* o) { o->pressure.bit_width = 0U; }, AOAHID_ERR_PARAM, nullptr},
            {[&](O* o) {
                 o->enable_tilt = 1U;
                 o->tilt_x = tilt;
                 o->tilt_y = tilt;
             },
             AOAHID_OK, nullptr},
            {[&](O* o) {
                 o->enable_tilt = 1U;
                 o->tilt_x = tilt;
             },
             AOAHID_ERR_PARAM, nullptr},
            {[&](O* o) {
                 o->enable_tilt = 1U;
                 o->tilt_y = tilt;
             },
             AOAHID_ERR_PARAM, nullptr},
            {[&](O* o) {
                 o->enable_twist_target_specific = 1U;
                 o->twist = aoahid_integer_field_make(0, 359, 16U);
             },
             AOAHID_OK, nullptr},
            {[](O* o) { o->enable_twist_target_specific = 1U; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) {
                 o->enable_pressure = 0U;
                 o->pressure = {};
                 o->enable_eraser = 1U;
                 o->barrel_usage_count = 0U;
                 o->barrel_usages = nullptr;
             },
             AOAHID_OK, nullptr},
            {[](O* o) { o->enable_pressure = 0U; }, AOAHID_ERR_PARAM, "pen.disabled_fields"},
            {[&](O* o) { o->tilt_x = tilt; }, AOAHID_ERR_PARAM, "pen.disabled_fields"},
            {[&](O* o) { o->tilt_y = tilt; }, AOAHID_ERR_PARAM, "pen.disabled_fields"},
            {[&](O* o) { o->twist = tilt; }, AOAHID_ERR_PARAM, "pen.disabled_fields"},
            {[](O* o) { o->barrel_usage_count = 0U; }, AOAHID_ERR_PARAM, "pen.disabled_fields"},
            {[](O* o) {
                 o->enable_tilt = 1U;
                 o->tilt_x = aoahid_integer_field_make(1, 90, 8U);
                 o->tilt_y = aoahid_integer_field_make(-90, 90, 8U);
             },
             AOAHID_ERR_PARAM, "pen.neutral_fields"},
            {[](O* o) {
                 o->enable_tilt = 1U;
                 o->tilt_x = aoahid_integer_field_make(-90, 90, 8U);
                 o->tilt_y = aoahid_integer_field_make(-90, -1, 8U);
             },
             AOAHID_ERR_PARAM, "pen.neutral_fields"},
            {[](O* o) {
                 o->enable_twist_target_specific = 1U;
                 o->twist = aoahid_integer_field_make(1, 359, 16U);
             },
             AOAHID_ERR_PARAM, "pen.neutral_fields"},
            {[](O* o) {
                 o->barrel_usages = both.data();
                 o->barrel_usage_count = both.size();
             },
             AOAHID_OK, nullptr},
            {[](O* o) {
                 o->barrel_usages = repeated.data();
                 o->barrel_usage_count = repeated.size();
             },
             AOAHID_ERR_PARAM, "pen.barrel_usages"},
            {[](O* o) { o->barrel_usages = eraser.data(); }, AOAHID_ERR_PARAM, "pen.barrel_usages"},
            {[](O* o) { o->enable_battery = 1U; }, AOAHID_ERR_PARAM, "pen.battery.report_id"},
        });

    aoahid_digitizer_options digitizer;
    aoahid_digitizer_options_init(&digitizer);
    using D = aoahid_digitizer_options;
    check_cases<D>(
        digitizer, aoahid_spec_create_digitizer,
        {
            {[](D* o) { o->reserved = 1U; }, AOAHID_ERR_PARAM, "digitizer_options"},
            {[](D* o) { o->enable_pressure = 0U; }, AOAHID_ERR_PARAM, "digitizer.disabled_fields"},
            {[](D* o) { o->barrel_usages = eraser.data(); }, AOAHID_ERR_PARAM,
             "digitizer.barrel_usages"},
            {[](D* o) { o->enable_battery = 1U; }, AOAHID_ERR_PARAM, "digitizer.battery.report_id"},
        });
    aoahid_capability_manifest manifest;
    aoahid_capability_manifest_init(&manifest);
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_digitizer(&digitizer, &spec) == AOAHID_OK &&
                 aoahid_spec_manifest(spec, &manifest) == AOAHID_OK &&
                 manifest.profile_kind == AOAHID_PROFILE_DIGITIZER &&
                 manifest.android_status == AOAHID_ANDROID_CONDITIONAL);
    aoahid_spec_release(spec);
}

void test_raw_options() {
    static constexpr std::array<std::uint8_t, 21U> descriptor{
        0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x85U, 0x01U, 0x15U, 0x00U, 0x25U,
        0x01U, 0x75U, 0x01U, 0x95U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U};
    static constexpr aoahid_raw_report report{{1U, 1U, {}}, 2U};
    aoahid_raw_options base;
    aoahid_raw_options_init(&base);
    base.descriptor = descriptor.data();
    base.descriptor_length = descriptor.size();
    base.reports = &report;
    base.report_count = 1U;
    base.acknowledges_no_android_support = 1U;
    using O = aoahid_raw_options;
    static constexpr aoahid_raw_report wrong_length{{1U, 1U, {}}, 3U};
    check_cases<O>(
        base, aoahid_spec_create_raw,
        {
            {[](O* o) { o->reserved = 1U; }, AOAHID_ERR_UNSUPPORTED, "raw.acknowledgements"},
            {[](O* o) { o->acknowledges_no_android_support = 0U; }, AOAHID_ERR_UNSUPPORTED,
             "raw.acknowledgements"},
            {[](O* o) { o->requires_output = 1U; }, AOAHID_ERR_UNSUPPORTED, "raw.acknowledgements"},
            {[](O* o) { o->requires_feature_response = 1U; }, AOAHID_ERR_UNSUPPORTED,
             "raw.acknowledgements"},
            {[](O* o) { o->reports = nullptr; }, AOAHID_ERR_UNSET_FIELD, "raw.reports"},
            {[](O* o) { o->report_count = 0U; }, AOAHID_ERR_UNSET_FIELD, "raw.reports"},
            {[](O* o) { o->report_count = 257U; }, AOAHID_ERR_OVERFLOW, "raw.reports"},
            {[](O* o) { o->reports = &wrong_length; }, AOAHID_ERR_PARAM, nullptr},
            {[](O* o) { o->descriptor = nullptr; }, AOAHID_ERR_UNSET_FIELD, "raw.descriptor"},
        });
}

void test_spec_handles() {
    aoahid_keyboard_options options;
    aoahid_keyboard_options_init(&options);
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&options, &spec) == AOAHID_OK);
    const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(&options);
    std::size_t length = 99U;
    AOAHID_CHECK(aoahid_spec_descriptor(nullptr, &bytes, &length) == AOAHID_ERR_PARAM &&
                 bytes == nullptr && length == 0U && field_is("spec"));
    AOAHID_CHECK(aoahid_spec_descriptor(spec, nullptr, &length) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_spec_descriptor(spec, &bytes, nullptr) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_spec_descriptor(spec, &bytes, &length) == AOAHID_OK && bytes != nullptr &&
                 length != 0U);
    aoahid_capability_manifest manifest;
    aoahid_capability_manifest_init(&manifest);
    AOAHID_CHECK(aoahid_spec_manifest(nullptr, &manifest) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_spec_manifest(spec, nullptr) == AOAHID_ERR_PARAM && field_is("manifest"));
    manifest.reserved = 1U;
    AOAHID_CHECK(aoahid_spec_manifest(spec, &manifest) == AOAHID_ERR_PARAM);
    manifest.reserved = 0U;
    manifest.struct_size = 4U;
    AOAHID_CHECK(aoahid_spec_manifest(spec, &manifest) == AOAHID_ERR_PARAM);

    // Reference counting: a null handle and a count that cannot grow are refused.
    aoahid_spec_retain(nullptr);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_ERR_PARAM && field_is("spec.retain"));
    aoahid_spec_release(nullptr);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_ERR_PARAM && field_is("spec.release"));
    aoahid_spec_retain(spec);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_OK && spec->references.load() == 2U);
    aoahid_spec_release(spec);
    AOAHID_CHECK(spec->references.load() == 1U);
    spec->references.store(std::numeric_limits<std::uint32_t>::max());
    aoahid_spec_retain(spec);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_ERR_OVERFLOW && field_is("spec.retain"));
    spec->references.store(1U);
    aoahid_spec_release(spec);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_OK);
}

} // namespace

void test_spec_options() {
    test_mouse_options();
    test_keyboard_options();
    test_toggle_options();
    test_gamepad_options();
    test_touch_options();
    test_stylus_options();
    test_raw_options();
    test_spec_handles();
}
