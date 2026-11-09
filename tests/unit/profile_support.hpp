// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* What the profile unit tests share: field lookup and extraction in a serialized
 * report, descriptor item search, and the options each profile test starts from.
 * The tests verify generated descriptors, layouts and state obligations in memory;
 * they do not claim target-device acceptance. */
#pragma once

#include "aoahid.h"
#include "api/internal.hpp"
#include "hid/descriptor_builder.hpp"
#include "hid/usages.hpp"
#include "hid/validator.hpp"
#include "test.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace profile_test {

static_assert(sizeof(aoahid_usage_semantic) == sizeof(std::int32_t));

inline std::uint64_t extract(const std::uint8_t* report, const aoa::hid::FieldLayout& field) {
    std::uint64_t value = 0U;
    for (std::uint8_t bit = 0U; bit < field.bit_width; ++bit) {
        const std::size_t position = field.bit_offset + bit;
        value |= static_cast<std::uint64_t>((report[position / 8U] >> (position & 7U)) & 1U) << bit;
    }
    return value;
}

inline const aoa::hid::FieldLayout* field(const aoahid_spec* spec,
                                          const aoa::hid::FieldSemantic semantic,
                                          const std::uint16_t instance = 0U) {
    for (const auto& candidate : spec->layout.fields) {
        if (candidate.semantic == semantic && candidate.instance == instance)
            return &candidate;
    }
    return nullptr;
}

struct PinnedLinuxPenTools {
    bool invert{};
    bool pen{};
    bool rubber{};
};

inline void apply_pinned_linux_pen_report(const aoahid_spec* spec, const std::uint8_t* report,
                                          PinnedLinuxPenTools* tools) {
    std::vector<const aoa::hid::FieldLayout*> ordered;
    for (const auto& candidate : spec->layout.fields) {
        if (candidate.semantic == aoa::hid::FieldSemantic::eraser ||
            candidate.semantic == aoa::hid::FieldSemantic::in_range) {
            ordered.push_back(&candidate);
        }
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
        return left->bit_offset < right->bit_offset;
    });
    for (const auto* candidate : ordered) {
        const bool asserted = extract(report, *candidate) != 0U;
        if (candidate->semantic == aoa::hid::FieldSemantic::eraser) {
            tools->invert = asserted;
        } else if (!asserted) {
            tools->pen = false;
            tools->rubber = false;
        } else if (tools->invert) {
            tools->rubber = true;
        } else {
            tools->pen = true;
        }
    }
}

inline std::size_t count_item(const std::vector<std::uint8_t>& descriptor,
                              const std::uint8_t prefix, const std::uint8_t value) {
    std::size_t count = 0U;
    for (std::size_t index = 1U; index < descriptor.size(); ++index) {
        if (descriptor[index - 1U] == prefix && descriptor[index] == value)
            ++count;
    }
    return count;
}

inline bool has_sequence(const std::vector<std::uint8_t>& descriptor,
                         const std::initializer_list<std::uint8_t> sequence) {
    return std::search(descriptor.begin(), descriptor.end(), sequence.begin(), sequence.end()) !=
           descriptor.end();
}

inline aoahid_keyboard_options keyboard_options() {
    aoahid_keyboard_options options{};
    options.struct_size = static_cast<std::uint32_t>(sizeof(options));
    options.reserved = 0U;
    options.report_id = {0U, 0U, {0U, 0U, 0U}};
    options.usage_minimum = 0x04U;
    options.usage_maximum = 0x65U;
    return options;
}

// A profile's valid options must also work with a battery report added, and
// the input side of the descriptor must not change.
template <typename Options>
inline void check_battery_variant(Options options,
                                  aoahid_result(AOAHID_CALL* create)(const Options*,
                                                                     aoahid_spec**)) {
    options.report_id = {1U, 1U, {0U, 0U, 0U}};
    aoahid_spec* plain = nullptr;
    AOAHID_CHECK(create(&options, &plain) == AOAHID_OK);
    options.enable_battery = 1U;
    options.battery.report_id = {1U, 2U, {0U, 0U, 0U}};
    options.battery.strength = {1, 100, 8U, {}};
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(create(&options, &spec) == AOAHID_OK);
    if (plain != nullptr && spec != nullptr) {
        // The input side is the plain descriptor without its closing End
        // Collection; the battery report is the 16 bytes before the new one.
        AOAHID_CHECK(spec->descriptor.size() == plain->descriptor.size() + 16U);
        AOAHID_CHECK(std::equal(plain->descriptor.begin(), plain->descriptor.end() - 1,
                                spec->descriptor.begin()));
        AOAHID_CHECK(spec->layout.wire_bytes == plain->layout.wire_bytes);
        AOAHID_CHECK(!plain->has_battery && spec->has_battery &&
                     spec->battery_layout.report_id == 2U && spec->battery_layout.wire_bytes == 2U);
        aoahid_capability_manifest manifest{};
        manifest.struct_size = static_cast<std::uint32_t>(sizeof(manifest));
        AOAHID_CHECK(aoahid_spec_manifest(spec, &manifest) == AOAHID_OK);
        AOAHID_CHECK(manifest.report_count == 2U && manifest.reports[0].report_id.value == 1U &&
                     manifest.reports[1].report_id.enabled == 1U &&
                     manifest.reports[1].report_id.value == 2U &&
                     manifest.reports[1].wire_length == 2U);
        AOAHID_CHECK(aoahid_spec_manifest(plain, &manifest) == AOAHID_OK);
        AOAHID_CHECK(manifest.report_count == 1U);
    }
    aoahid_spec_release(plain);
    aoahid_spec_release(spec);
}

// One pressed/transition slot per allowed Usage, as aoahid_node_open builds it.
inline aoa::detail::ConsumerState consumer_state_for(const aoahid_spec* spec) {
    const auto* config = std::get_if<aoa::detail::ConsumerConfig>(&spec->config);
    aoa::detail::ConsumerState state{};
    state.pressed.resize(config->allowed_usages.size());
    state.transitions.resize(config->allowed_usages.size());
    return state;
}

inline aoahid_touchscreen_options touchscreen_options() {
    aoahid_touchscreen_options options{};
    options.struct_size = static_cast<std::uint32_t>(sizeof(options));
    options.report_id = {0U, 0U, {0U, 0U, 0U}};
    options.maximum_contacts = 3U;
    options.contacts_per_report = 2U;
    options.contact_id = {0, 15, 4U, {}};
    options.x = {0, 1000, 16U, {}};
    options.y = {0, 1000, 16U, {}};
    options.contact_count = {0, 3, 2U, {}};
    options.enable_pressure = 1U;
    options.pressure = {0, 255, 8U, {}};
    options.enable_width = 0U;
    options.width = {0, 0, 0U, {}};
    options.enable_height = 0U;
    options.height = {0, 0, 0U, {}};
    options.enable_scan_time = 1U;
    options.scan_time = {0, 65535, 16U, {}};
    options.scan_time_unit_100us = 1U;
    options.enable_contact_count_maximum_feature_declaration = 0U;
    options.enable_multi_packet_frames = 1U;
    return options;
}

inline aoahid_touchpad_options touchpad_options() {
    aoahid_touchpad_options options{};
    options.struct_size = static_cast<std::uint32_t>(sizeof(options));
    options.report_id = {0U, 0U, {0U, 0U, 0U}};
    options.maximum_contacts = 3U;
    options.contacts_per_report = 2U;
    options.contact_id = {0, 15, 4U, {}};
    options.x = {0, 1000, 16U, {}};
    options.y = {0, 1000, 16U, {}};
    options.contact_count = {0, 3, 2U, {}};
    options.enable_pressure = 1U;
    options.pressure = {0, 255, 8U, {}};
    options.enable_width = 0U;
    options.width = {0, 0, 0U, {}};
    options.enable_height = 0U;
    options.height = {0, 0, 0U, {}};
    options.enable_scan_time = 1U;
    options.scan_time = {0, 65535, 16U, {}};
    options.scan_time_unit_100us = 1U;
    options.enable_contact_count_maximum_feature_declaration = 0U;
    options.enable_multi_packet_frames = 1U;
    options.button_count = 0U;
    return options;
}

// The tests, in the order test_profiles() runs them.
void test_keyboard_nkro_concurrent_keys();
void test_keyboard_bitmap_and_lifecycle_transitions();
void test_mouse_delta_splitting_and_consumption();
void test_usage_control_semantics();
void test_hat_domain();
void test_controller_dpad_and_extended_axes();
void test_physical_properties_and_reset();
void test_field_span_padding();
void test_touch_units_feature_and_manifest_status();
void test_touch_azimuth();
void test_touch_packets();
void test_touch_lift_frame_and_idle_suppression();
void test_touchpad_button_only_and_mid_frame_guard();
void test_pen_away_coordinates();
void test_pen_switch();
void test_raw_report_id_order();
void test_raw_report_table_report_id_rules();
void test_raw_factory_bounds_before_allocation();
void test_send_time_report_validation();
void test_battery_builder();
void test_battery_options();
void test_battery_state();

} // namespace profile_test
