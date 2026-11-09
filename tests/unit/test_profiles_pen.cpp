// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Pen and digitizer: coordinates while away, and switching between tools. */
#include "unit/profile_support.hpp"

namespace profile_test {

void test_pen_away_coordinates() {
    const std::array<std::uint16_t, 1> barrels{0x44U};
    aoahid_pen_options options{};
    options.struct_size = static_cast<std::uint32_t>(sizeof(options));
    options.report_id = {0U, 0U, {0U, 0U, 0U}};
    options.x = {0, 1000, 16U, {}};
    options.y = {0, 1000, 16U, {}};
    options.enable_pressure = 1U;
    options.pressure = {0, 255, 8U, {}};
    options.barrel_usages = barrels.data();
    options.barrel_usage_count = barrels.size();
    options.enable_hover = 1U;
    aoahid_spec* spec = nullptr;
    check_battery_variant(options, &aoahid_spec_create_pen);
    AOAHID_CHECK(aoahid_spec_create_pen(&options, &spec) == AOAHID_OK);
    if (spec == nullptr)
        return;
    const auto* x = field(spec, aoa::hid::FieldSemantic::x);
    const auto* y = field(spec, aoa::hid::FieldSemantic::y);
    AOAHID_CHECK(x != nullptr && y != nullptr);
    std::array<std::uint8_t, 128> report{};
    std::size_t length = 0U;
    aoahid_node node{};
    node.spec = spec;
    node.state = aoa::detail::PenState{};
    aoahid_pen_sample sample{};
    sample.in_range = 1U;
    sample.x = 100;
    sample.y = 200;
    AOAHID_CHECK(aoahid_pen_update(&node, &sample) == AOAHID_OK);
    node.transfer_inflight.store(true);
    aoa::detail::transfer_complete(&node, AOAHID_OK, 0);
    // The away report carries the sample's own X/Y, not the last position.
    sample.in_range = 0U;
    sample.x = 500;
    sample.y = 600;
    AOAHID_CHECK(aoahid_pen_update(&node, &sample) == AOAHID_OK);
    report.fill(0U);
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    const auto* range = field(spec, aoa::hid::FieldSemantic::in_range);
    AOAHID_CHECK(range != nullptr && extract(report.data(), *range) == 0U);
    AOAHID_CHECK(x != nullptr && extract(report.data(), *x) == 500U);
    AOAHID_CHECK(y != nullptr && extract(report.data(), *y) == 600U);
    aoahid_spec_release(spec);
}

void test_pen_switch() {
    const std::array<std::uint16_t, 1> barrels{0x44U};
    aoahid_pen_options options{};
    options.struct_size = static_cast<std::uint32_t>(sizeof(options));
    options.report_id = {0U, 0U, {0U, 0U, 0U}};
    options.x = {0, 1000, 16U, {}};
    options.y = {0, 1000, 16U, {}};
    options.enable_pressure = 1U;
    options.pressure = {0, 255, 8U, {}};
    options.enable_tilt = 0U;
    options.enable_twist_target_specific = 0U;
    options.barrel_usages = barrels.data();
    options.barrel_usage_count = barrels.size();
    options.enable_eraser = 1U;
    options.enable_hover = 1U;
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_pen(&options, &spec) == AOAHID_OK);
    if (spec == nullptr)
        return;
    aoahid_capability_manifest manifest{};
    manifest.struct_size = static_cast<std::uint32_t>(sizeof(manifest));
    AOAHID_CHECK(aoahid_spec_manifest(spec, &manifest) == AOAHID_OK);
    AOAHID_CHECK(manifest.android_status == AOAHID_ANDROID_PORTABLE_CANDIDATE);
    aoahid_node node{};
    node.spec = spec;
    aoa::detail::PenState state_value{};
    state_value.emitted.in_range = 1U;
    state_value.emitted.barrel_buttons = 1U;
    state_value.emitted.x = 100;
    state_value.emitted.y = 200;
    node.state = state_value;
    aoahid_pen_sample sample{};
    sample.in_range = 1U;
    sample.eraser = 1U;
    sample.barrel_buttons = 1U;
    sample.x = 100;
    sample.y = 200;
    AOAHID_CHECK(aoahid_pen_update(&node, &sample) == AOAHID_OK);
    sample.in_range = 0U;
    AOAHID_CHECK(aoahid_pen_update(&node, &sample) == AOAHID_ERR_BUSY);
    std::array<std::uint8_t, 128> report{};
    std::size_t length = 0U;
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    const auto* range = field(spec, aoa::hid::FieldSemantic::in_range);
    const auto* invert = field(spec, aoa::hid::FieldSemantic::eraser);
    const auto* barrel = field(spec, aoa::hid::FieldSemantic::buttons);
    const auto* tip = field(spec, aoa::hid::FieldSemantic::tip);
    AOAHID_CHECK(invert != nullptr && tip != nullptr && range != nullptr &&
                 invert->bit_offset < tip->bit_offset && tip->bit_offset < range->bit_offset);
    AOAHID_CHECK(range != nullptr && extract(report.data(), *range) == 0U);
    AOAHID_CHECK(invert != nullptr && extract(report.data(), *invert) == 0U);
    AOAHID_CHECK(barrel != nullptr && extract(report.data(), *barrel) == 0U);
    // Model the order-sensitive tool selection in Android common commit
    // 35556bed836f8dc07ac55f69c8d17dce3e7f0e25. Departure releases both tools;
    // Invert must then be processed before In Range on eraser re-entry.
    PinnedLinuxPenTools tools{false, true, false};
    apply_pinned_linux_pen_report(spec, report.data(), &tools);
    AOAHID_CHECK(!tools.pen && !tools.rubber);
    node.transfer_inflight.store(true);
    aoa::detail::transfer_complete(&node, AOAHID_OK, 0);
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    AOAHID_CHECK(range != nullptr && extract(report.data(), *range) == 1U);
    AOAHID_CHECK(invert != nullptr && extract(report.data(), *invert) == 1U);
    apply_pinned_linux_pen_report(spec, report.data(), &tools);
    AOAHID_CHECK(!tools.pen && tools.rubber);

    // Commit the eraser-side arrival, then leave range without making the
    // caller clear the remembered tool selector. The wire report must still
    // clear Invert so current Linux tool state can release BTN_TOOL_RUBBER.
    node.transfer_inflight.store(true);
    aoa::detail::transfer_complete(&node, AOAHID_OK, 0);
    sample.in_range = 0U;
    AOAHID_CHECK(aoahid_pen_update(&node, &sample) == AOAHID_OK);
    report.fill(0U);
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    const auto* pressure = field(spec, aoa::hid::FieldSemantic::pressure);
    AOAHID_CHECK(range != nullptr && extract(report.data(), *range) == 0U);
    AOAHID_CHECK(tip != nullptr && extract(report.data(), *tip) == 0U);
    AOAHID_CHECK(invert != nullptr && extract(report.data(), *invert) == 0U);
    AOAHID_CHECK(barrel != nullptr && extract(report.data(), *barrel) == 0U);
    AOAHID_CHECK(pressure != nullptr && extract(report.data(), *pressure) == 0U);

    // Re-enter on the remembered eraser side, then switch back to the pen side.
    node.transfer_inflight.store(true);
    aoa::detail::transfer_complete(&node, AOAHID_OK, 0);
    sample.in_range = 1U;
    sample.eraser = 1U;
    AOAHID_CHECK(aoahid_pen_update(&node, &sample) == AOAHID_OK);
    report.fill(0U);
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    AOAHID_CHECK(range != nullptr && extract(report.data(), *range) == 1U);
    AOAHID_CHECK(invert != nullptr && extract(report.data(), *invert) == 1U);
    node.transfer_inflight.store(true);
    aoa::detail::transfer_complete(&node, AOAHID_OK, 0);
    sample.eraser = 0U;
    AOAHID_CHECK(aoahid_pen_update(&node, &sample) == AOAHID_OK);
    report.fill(0U);
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    AOAHID_CHECK(range != nullptr && extract(report.data(), *range) == 0U);
    AOAHID_CHECK(invert != nullptr && extract(report.data(), *invert) == 0U);
    AOAHID_CHECK(barrel != nullptr && extract(report.data(), *barrel) == 0U);
    aoahid_spec_release(spec);

    aoahid_digitizer_options digitizer{};
    digitizer.struct_size = static_cast<std::uint32_t>(sizeof(digitizer));
    digitizer.report_id = options.report_id;
    digitizer.x = options.x;
    digitizer.y = options.y;
    digitizer.enable_pressure = options.enable_pressure;
    digitizer.pressure = options.pressure;
    digitizer.barrel_usages = options.barrel_usages;
    digitizer.barrel_usage_count = options.barrel_usage_count;
    digitizer.enable_eraser = options.enable_eraser;
    digitizer.enable_hover = options.enable_hover;
    spec = nullptr;
    check_battery_variant(digitizer, &aoahid_spec_create_digitizer);
    AOAHID_CHECK(aoahid_spec_create_digitizer(&digitizer, &spec) == AOAHID_OK);
    if (spec != nullptr) {
        manifest = {};
        manifest.struct_size = static_cast<std::uint32_t>(sizeof(manifest));
        AOAHID_CHECK(aoahid_spec_manifest(spec, &manifest) == AOAHID_OK);
        AOAHID_CHECK(manifest.profile_kind == AOAHID_PROFILE_DIGITIZER);
        AOAHID_CHECK(manifest.android_status == AOAHID_ANDROID_CONDITIONAL);
        aoahid_node digitizer_node{};
        digitizer_node.spec = spec;
        digitizer_node.state = aoa::detail::PenState{};
        digitizer_node.transfer_inflight.store(true);
        aoa::detail::transfer_complete(&digitizer_node, AOAHID_OK, 0);
        aoahid_digitizer_sample digitizer_sample{};
        digitizer_sample.in_range = 1U;
        digitizer_sample.x = 10;
        digitizer_sample.y = 20;
        AOAHID_CHECK(aoahid_digitizer_update(&digitizer_node, &digitizer_sample) == AOAHID_OK);
        digitizer_sample.in_range = 0U;
        AOAHID_CHECK(aoahid_digitizer_update(&digitizer_node, &digitizer_sample) !=
                     AOAHID_ERR_PARAM);
        aoahid_spec_release(spec);
    }
}

} // namespace profile_test
