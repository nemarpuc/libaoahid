// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* The "profiles" suite: the checks every profile shares (rejected options, error fields
 * that name the profile, the ABI profile numbers), then the per-profile tests of the
 * test_profiles_*.cpp files next to this one. */
#include "unit/profile_support.hpp"

#include <string_view>

namespace profile_test {

// Every options error names the structure the caller filled in, not the
// implementation two profiles share.
void test_error_fields_name_their_profile() {
    const auto field_starts_with = [](const std::string_view prefix) {
        const char* field = aoahid_last_error()->field;
        return field != nullptr && std::string_view(field).substr(0, prefix.size()) == prefix;
    };
    aoahid_spec* spec = nullptr;
    aoahid_touchscreen_options touchscreen = touchscreen_options();
    touchscreen.x = {};
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&touchscreen, &spec) != AOAHID_OK);
    AOAHID_CHECK(field_starts_with("touchscreen"));
    aoahid_touchpad_options touchpad = touchpad_options();
    touchpad.x = {};
    AOAHID_CHECK(aoahid_spec_create_touchpad(&touchpad, &spec) != AOAHID_OK);
    AOAHID_CHECK(field_starts_with("touchpad"));
    aoahid_pen_options pen{};
    pen.struct_size = static_cast<std::uint32_t>(sizeof(pen));
    AOAHID_CHECK(aoahid_spec_create_pen(&pen, &spec) != AOAHID_OK);
    AOAHID_CHECK(field_starts_with("pen"));
    aoahid_digitizer_options digitizer{};
    digitizer.struct_size = static_cast<std::uint32_t>(sizeof(digitizer));
    AOAHID_CHECK(aoahid_spec_create_digitizer(&digitizer, &spec) != AOAHID_OK);
    AOAHID_CHECK(field_starts_with("digitizer"));
}

} // namespace profile_test

void test_profiles() {
    using profile_test::keyboard_options;
    using profile_test::touchpad_options;
    using profile_test::touchscreen_options;
    aoahid_keyboard_options zero{};
    auto* spec = reinterpret_cast<aoahid_spec*>(static_cast<std::uintptr_t>(1U));
    AOAHID_CHECK(aoahid_spec_create_keyboard(&zero, &spec) != AOAHID_OK);
    AOAHID_CHECK(spec == nullptr);
    AOAHID_CHECK(aoahid_spec_create_keyboard(&zero, nullptr) == AOAHID_ERR_PARAM);

    aoahid_keyboard_options invalid_keyboard = keyboard_options();
    invalid_keyboard.report_id.reserved8[0] = 1U;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&invalid_keyboard, &spec) == AOAHID_ERR_PARAM);
    invalid_keyboard = keyboard_options();
    invalid_keyboard.usage_minimum = 1U;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&invalid_keyboard, &spec) == AOAHID_ERR_PARAM);
    invalid_keyboard = keyboard_options();
    invalid_keyboard.usage_maximum =
        static_cast<std::uint16_t>(invalid_keyboard.usage_minimum - 1U);
    AOAHID_CHECK(aoahid_spec_create_keyboard(&invalid_keyboard, &spec) == AOAHID_ERR_PARAM);
    invalid_keyboard = keyboard_options();
    invalid_keyboard.usage_maximum = 0xE0U;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&invalid_keyboard, &spec) == AOAHID_ERR_PARAM);

    profile_test::test_keyboard_nkro_concurrent_keys();
    profile_test::test_keyboard_bitmap_and_lifecycle_transitions();
    profile_test::test_mouse_delta_splitting_and_consumption();
    profile_test::test_usage_control_semantics();
    profile_test::test_hat_domain();
    profile_test::test_controller_dpad_and_extended_axes();
    profile_test::test_physical_properties_and_reset();
    profile_test::test_field_span_padding();
    profile_test::test_touch_units_feature_and_manifest_status();
    profile_test::test_touch_azimuth();
    profile_test::test_touch_packets();
    profile_test::test_touch_lift_frame_and_idle_suppression();
    profile_test::test_touchpad_button_only_and_mid_frame_guard();
    profile_test::test_pen_away_coordinates();
    profile_test::test_pen_switch();
    profile_test::test_raw_report_id_order();
    profile_test::test_raw_report_table_report_id_rules();
    profile_test::test_raw_factory_bounds_before_allocation();
    profile_test::test_send_time_report_validation();
    profile_test::test_battery_builder();
    profile_test::test_battery_options();
    profile_test::test_battery_state();
    profile_test::test_error_fields_name_their_profile();

    aoahid_touchscreen_options invalid_touch = touchscreen_options();
    invalid_touch.contact_id = {1, 15, 4U, {}};
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&invalid_touch, &spec) == AOAHID_ERR_PARAM);

    aoahid_touchpad_options invalid_touchpad = touchpad_options();
    invalid_touchpad.button_count = 65536U;
    AOAHID_CHECK(aoahid_spec_create_touchpad(&invalid_touchpad, &spec) == AOAHID_ERR_OVERFLOW);

    aoahid_touchpad_options buttonless = touchpad_options();
    buttonless.button_count = 0U;
    AOAHID_CHECK(aoahid_spec_create_touchpad(&buttonless, &spec) == AOAHID_OK);
    if (spec != nullptr) {
        aoahid_capability_manifest manifest{};
        manifest.struct_size = static_cast<std::uint32_t>(sizeof(manifest));
        AOAHID_CHECK(aoahid_spec_manifest(spec, &manifest) == AOAHID_OK);
        AOAHID_CHECK(manifest.profile_kind == AOAHID_PROFILE_TOUCHPAD);
        AOAHID_CHECK(manifest.android_status == AOAHID_ANDROID_CONDITIONAL);
    }
    aoahid_spec_release(spec);
    spec = nullptr;

    // The profile numbers are part of the ABI.
    AOAHID_CHECK(AOAHID_PROFILE_KEYBOARD == 1 && AOAHID_PROFILE_MOUSE == 2 &&
                 AOAHID_PROFILE_TOUCHSCREEN == 3 && AOAHID_PROFILE_TOUCHPAD == 4 &&
                 AOAHID_PROFILE_PEN == 5 && AOAHID_PROFILE_DIGITIZER == 6 &&
                 AOAHID_PROFILE_GAMEPAD == 7 && AOAHID_PROFILE_TOGGLE == 8 &&
                 AOAHID_PROFILE_RAW == 9);
}
