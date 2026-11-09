// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Touchscreen and touchpad: units, feature reports and manifest status, azimuth,
 * multi-packet frames, the lift frame and idle suppression, and a button-only touchpad. */
#include "unit/profile_support.hpp"

namespace profile_test {

void test_touch_units_feature_and_manifest_status() {
    aoahid_touchscreen_options options = touchscreen_options();
    options.enable_contact_count_maximum_feature_declaration = 1U;
    aoahid_spec* spec = nullptr;
    check_battery_variant(options, &aoahid_spec_create_touchscreen);
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&options, &spec) == AOAHID_OK);
    if (spec != nullptr) {
        AOAHID_CHECK(count_item(spec->descriptor, 0xB1U, 0x03U) == 1U);
        aoahid_spec_release(spec);
    }

    options = touchscreen_options();
    options.x.physical = {1U, 0, 100, -2, 0x11U};
    options.enable_width = 1U;
    options.width = {0, 100, 8U, {1U, 0, 10, -2, 0x11U}};
    spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&options, &spec) == AOAHID_OK);
    aoahid_spec_release(spec);
    options.width.physical.unit_exponent = -3;
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&options, &spec) == AOAHID_ERR_PARAM);

    options = touchscreen_options();
    options.y.physical = {1U, 0, 100, -2, 0x11U};
    options.enable_height = 1U;
    options.height = {0, 100, 8U, {1U, 0, 10, -2, 0x12U}};
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&options, &spec) == AOAHID_ERR_PARAM);

    options = touchscreen_options();
    options.scan_time.physical = {1U, 0, 65535, -4, 0x1001U};
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&options, &spec) == AOAHID_ERR_PARAM);
}

void test_touch_azimuth() {
    aoahid_touchscreen_options options = touchscreen_options();
    options.enable_azimuth = 1U;
    options.azimuth = {0, 36000, 16U, {1U, 0, 360, 0, 0x14U}};
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&options, &spec) == AOAHID_OK);
    if (spec == nullptr)
        return;

    const auto* azimuth = field(spec, aoa::hid::FieldSemantic::azimuth, 0U);
    AOAHID_CHECK(azimuth != nullptr && azimuth->logical_minimum == 0 &&
                 azimuth->logical_maximum == 36000);
    aoahid_node node{};
    node.spec = spec;
    node.state = aoa::detail::TouchState{};
    const aoa::detail::TouchContact contact{1U, 100, 200, 10, 0, 0, 35999};
    aoahid_touchscreen_extra extra{contact.pressure, contact.width, contact.height,
                                   contact.azimuth};
    AOAHID_CHECK(aoahid_touchscreen_contact(&node, contact.contact_id, 1U, contact.x, contact.y,
                                            &extra) == AOAHID_OK);
    std::array<std::uint8_t, 128U> report{};
    std::size_t length = 0U;
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    AOAHID_CHECK(azimuth != nullptr && extract(report.data(), *azimuth) == 35999U);
    node.dirty = false;
    AOAHID_CHECK(aoahid_touchscreen_contact(&node, contact.contact_id, 1U, contact.x, contact.y,
                                            &extra) == AOAHID_OK);
    AOAHID_CHECK(!node.dirty);
    aoahid_touchscreen_extra invalid_extra = extra;
    invalid_extra.azimuth = 36000;
    AOAHID_CHECK(aoahid_touchscreen_contact(&node, contact.contact_id, 1U, contact.x, contact.y,
                                            &invalid_extra) == AOAHID_ERR_PARAM);
    aoahid_spec_release(spec);

    options.azimuth.logical_minimum = 1;
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&options, &spec) == AOAHID_ERR_PARAM);
    options.enable_azimuth = 0U;
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&options, &spec) == AOAHID_ERR_PARAM);
    options.azimuth = {};
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&options, &spec) == AOAHID_OK);
    if (spec != nullptr) {
        aoahid_node disabled_node{};
        disabled_node.spec = spec;
        disabled_node.state = aoa::detail::TouchState{};
        aoahid_touchscreen_extra disabled_extra{10, 0, 0, 1};
        AOAHID_CHECK(aoahid_touchscreen_contact(&disabled_node, 1U, 1U, 100, 200,
                                                &disabled_extra) == AOAHID_ERR_PARAM);
    }
    aoahid_spec_release(spec);
}

void test_touch_packets() {
    aoahid_touchscreen_options options = touchscreen_options();
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&options, &spec) == AOAHID_OK);
    if (spec == nullptr)
        return;
    aoahid_node node{};
    node.spec = spec;
    node.state = aoa::detail::TouchState{};
    for (std::uint32_t id = 0U; id < 3U; ++id) {
        const aoa::detail::TouchContact contact{id,
                                                static_cast<std::int32_t>(100U + id),
                                                static_cast<std::int32_t>(200U + id),
                                                id == 0U ? 0 : 10,
                                                0,
                                                0,
                                                0};
        aoahid_touchscreen_extra extra{contact.pressure, contact.width, contact.height,
                                       contact.azimuth};
        AOAHID_CHECK(aoahid_touchscreen_contact(&node, contact.contact_id, 1U, contact.x, contact.y,
                                                &extra) == AOAHID_OK);
    }
    std::array<std::uint8_t, 128> report{};
    std::size_t length = 0U;
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    const auto* count = field(spec, aoa::hid::FieldSemantic::contact_count);
    const auto* pressure = field(spec, aoa::hid::FieldSemantic::pressure, 0U);
    const auto* scan_time = field(spec, aoa::hid::FieldSemantic::scan_time);
    AOAHID_CHECK(count != nullptr && extract(report.data(), *count) == 3U);
    AOAHID_CHECK(pressure != nullptr && extract(report.data(), *pressure) == 1U);
    AOAHID_CHECK(scan_time != nullptr && extract(report.data(), *scan_time) == 0U);
    node.transfer_inflight.store(true);
    aoa::detail::transfer_complete(&node, AOAHID_OK, 0);
    const aoa::detail::TouchContact changed{0U, 300, 400, 10, 0, 0, 0};
    aoahid_touchscreen_extra changed_extra{changed.pressure, changed.width, changed.height,
                                           changed.azimuth};
    AOAHID_CHECK(aoahid_touchscreen_contact(&node, changed.contact_id, 1U, changed.x, changed.y,
                                            &changed_extra) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    AOAHID_CHECK(count != nullptr && extract(report.data(), *count) == 0U);
    AOAHID_CHECK(scan_time != nullptr && extract(report.data(), *scan_time) == 0U);
    aoahid_spec_release(spec);
}

void test_touch_lift_frame_and_idle_suppression() {
    aoahid_touchscreen_options options = touchscreen_options();
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&options, &spec) == AOAHID_OK);
    if (spec == nullptr)
        return;

    aoahid_node node{};
    node.spec = spec;
    node.state = aoa::detail::TouchState{};
    const aoa::detail::TouchContact contact{7U, 100, 200, 0, 0, 0, 0};
    AOAHID_CHECK(aoahid_touchscreen_contact(&node, contact.contact_id, 1U, contact.x, contact.y,
                                            nullptr) == AOAHID_OK);
    AOAHID_CHECK(aoahid_touchscreen_contact(&node, 7U, 0U, 300, 400, nullptr) == AOAHID_ERR_BUSY);

    const auto* count = field(spec, aoa::hid::FieldSemantic::contact_count);
    const auto* tip = field(spec, aoa::hid::FieldSemantic::tip);
    const auto* identifier = field(spec, aoa::hid::FieldSemantic::contact_id);
    const auto* x = field(spec, aoa::hid::FieldSemantic::x);
    const auto* y = field(spec, aoa::hid::FieldSemantic::y);
    const auto* pressure = field(spec, aoa::hid::FieldSemantic::pressure);
    const auto* scan_time = field(spec, aoa::hid::FieldSemantic::scan_time);
    std::array<std::uint8_t, 128U> report{};
    std::size_t length = 0U;
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    AOAHID_CHECK(count != nullptr && extract(report.data(), *count) == 1U);
    AOAHID_CHECK(tip != nullptr && extract(report.data(), *tip) == 1U);
    AOAHID_CHECK(identifier != nullptr && extract(report.data(), *identifier) == 7U);
    AOAHID_CHECK(x != nullptr && extract(report.data(), *x) == 100U);
    AOAHID_CHECK(y != nullptr && extract(report.data(), *y) == 200U);
    AOAHID_CHECK(pressure != nullptr && extract(report.data(), *pressure) == 1U);
    AOAHID_CHECK(scan_time != nullptr && extract(report.data(), *scan_time) == 0U);
    aoa::detail::transfer_complete(&node, AOAHID_OK, 0);
    AOAHID_CHECK(!node.dirty);

    AOAHID_CHECK(aoahid_touchscreen_contact(&node, 7U, 0U, 300, 400, nullptr) == AOAHID_OK);
    report.fill(0U);
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    // The explicit Up report keeps the same slot and final coordinates while
    // clearing Tip; it is still counted until its successful completion.
    AOAHID_CHECK(count != nullptr && extract(report.data(), *count) == 1U);
    AOAHID_CHECK(tip != nullptr && extract(report.data(), *tip) == 0U);
    AOAHID_CHECK(identifier != nullptr && extract(report.data(), *identifier) == 7U);
    AOAHID_CHECK(x != nullptr && extract(report.data(), *x) == 300U);
    AOAHID_CHECK(y != nullptr && extract(report.data(), *y) == 400U);
    AOAHID_CHECK(pressure != nullptr && extract(report.data(), *pressure) == 0U);
    aoa::detail::transfer_complete(&node, AOAHID_OK, 0);

    const auto* state_value = std::get_if<aoa::detail::TouchState>(&node.state);
    AOAHID_CHECK(state_value != nullptr);
    if (state_value != nullptr) {
        AOAHID_CHECK(std::all_of(state_value->contacts.begin(), state_value->contacts.end(),
                                 [](const aoa::detail::ContactState& entry) {
                                     return entry.phase == aoa::detail::ContactPhase::none;
                                 }));
    }
    // Idle touch state is clean, so the public submit path suppresses a
    // zero-count report instead of sending one and reusing the previous count.
    AOAHID_CHECK(!node.dirty);
    AOAHID_CHECK(aoahid_touchscreen_contact(&node, 7U, 0U, 300, 400, nullptr) == AOAHID_ERR_PARAM);
    const aoa::detail::TouchContact next_contact{8U, 500, 600, 10, 0, 0, 0};
    aoahid_touchscreen_extra next_extra{next_contact.pressure, next_contact.width,
                                        next_contact.height, next_contact.azimuth};
    AOAHID_CHECK(aoahid_touchscreen_contact(&node, next_contact.contact_id, 1U, next_contact.x,
                                            next_contact.y, &next_extra) == AOAHID_OK);
    report.fill(0U);
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    AOAHID_CHECK(scan_time != nullptr && extract(report.data(), *scan_time) == 0U);
    aoahid_spec_release(spec);
}

void test_touchpad_button_only_and_mid_frame_guard() {
    aoahid_touchpad_options options = touchpad_options();
    options.button_count = 1U;
    aoahid_spec* spec = nullptr;
    check_battery_variant(options, &aoahid_spec_create_touchpad);
    AOAHID_CHECK(aoahid_spec_create_touchpad(&options, &spec) == AOAHID_OK);
    if (spec == nullptr)
        return;

    aoahid_node node{};
    node.spec = spec;
    aoa::detail::TouchState state_value{};
    state_value.buttons.resize(1U);
    state_value.button_transitions.resize(1U);
    node.state = std::move(state_value);

    AOAHID_CHECK(aoahid_touchpad_button(&node, 1U, 1U) == AOAHID_OK);
    std::array<std::uint8_t, 128U> report{};
    std::size_t length = 0U;
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    const auto* count = field(spec, aoa::hid::FieldSemantic::contact_count);
    const auto* button = field(spec, aoa::hid::FieldSemantic::buttons);
    AOAHID_CHECK(count != nullptr && extract(report.data(), *count) == 0U);
    AOAHID_CHECK(button != nullptr && extract(report.data(), *button) == 1U);
    AOAHID_CHECK(aoahid_touchpad_button(&node, 1U, 0U) == AOAHID_ERR_BUSY);
    aoa::detail::transfer_complete(&node, AOAHID_OK, 0);

    auto* touch = std::get_if<aoa::detail::TouchState>(&node.state);
    AOAHID_CHECK(touch != nullptr);
    if (touch != nullptr) {
        touch->packet_cursor = 1U;
        AOAHID_CHECK(aoahid_touchpad_button(&node, 1U, 0U) == AOAHID_ERR_BUSY);
        AOAHID_CHECK(touch->buttons[0] == 1U);
        touch->packet_cursor = 0U;
    }

    AOAHID_CHECK(aoahid_touchpad_button(&node, 1U, 0U) == AOAHID_OK);
    report.fill(0U);
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    AOAHID_CHECK(count != nullptr && extract(report.data(), *count) == 0U);
    AOAHID_CHECK(button != nullptr && extract(report.data(), *button) == 0U);
    aoahid_spec_release(spec);
}

} // namespace profile_test
