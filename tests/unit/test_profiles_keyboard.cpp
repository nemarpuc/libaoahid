// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Keyboard: N-key rollover, the modifier bitmap and its lifecycle transitions, and the
 * send-time check of a serialized report. */
#include "unit/profile_support.hpp"

namespace profile_test {

void test_keyboard_nkro_concurrent_keys() {
    aoahid_keyboard_options options = keyboard_options();
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&options, &spec) == AOAHID_OK);
    if (spec == nullptr)
        return;
    aoahid_node node{};
    node.spec = spec;
    aoa::detail::KeyboardState state_value{};
    const std::size_t key_count =
        static_cast<std::size_t>(options.usage_maximum - options.usage_minimum) + 1U;
    state_value.pressed_bitmap.resize(key_count);
    state_value.key_transitions.resize(key_count);
    node.state = std::move(state_value);
    AOAHID_CHECK(aoahid_keyboard_key(&node, 0xE0U, 1U) == AOAHID_OK);
    node.dirty = false;
    AOAHID_CHECK(aoahid_keyboard_key(&node, 0xE0U, 1U) == AOAHID_OK);
    AOAHID_CHECK(!node.dirty);

    // Twenty simultaneously held non-modifier keys: well beyond the legacy
    // six-slot Array limit. Full NKRO gives every Usage its own bitmap bit,
    // so there is no ErrorRollOver overflow to hit.
    constexpr std::uint16_t k_held_keys = 20U;
    for (std::uint16_t usage = 0x04U; usage < 0x04U + k_held_keys; ++usage)
        AOAHID_CHECK(aoahid_keyboard_key(&node, usage, 1U) == AOAHID_OK);

    std::array<std::uint8_t, 64> report{};
    std::size_t length = 0U;
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    AOAHID_CHECK(report[0] == 0x01U);
    for (std::uint16_t usage = 0x04U; usage < 0x04U + k_held_keys; ++usage) {
        const auto* key = field(spec, aoa::hid::FieldSemantic::key_bitmap,
                                static_cast<std::uint16_t>(usage - options.usage_minimum));
        AOAHID_CHECK(key != nullptr && extract(report.data(), *key) == 1U);
    }
    aoa::detail::transfer_complete(&node, AOAHID_OK, 0);

    AOAHID_CHECK(aoahid_keyboard_key(&node, 0x04U, 0U) == AOAHID_OK);
    report.fill(0U);
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    const auto* released = field(spec, aoa::hid::FieldSemantic::key_bitmap, 0U);
    AOAHID_CHECK(released != nullptr && extract(report.data(), *released) == 0U);
    for (std::uint16_t usage = 0x05U; usage < 0x04U + k_held_keys; ++usage) {
        const auto* key = field(spec, aoa::hid::FieldSemantic::key_bitmap,
                                static_cast<std::uint16_t>(usage - options.usage_minimum));
        AOAHID_CHECK(key != nullptr && extract(report.data(), *key) == 1U);
    }
    AOAHID_CHECK(aoahid_keyboard_key(&node, 0x66U, 0U) == AOAHID_ERR_PARAM);
    aoahid_spec_release(spec);
}

void test_keyboard_bitmap_and_lifecycle_transitions() {
    aoahid_keyboard_options options = keyboard_options();
    options.usage_maximum = 0x0BU;
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&options, &spec) == AOAHID_OK);
    if (spec == nullptr)
        return;

    aoahid_node node{};
    node.spec = spec;
    aoa::detail::KeyboardState state_value{};
    const std::size_t key_count =
        static_cast<std::size_t>(options.usage_maximum - options.usage_minimum) + 1U;
    state_value.pressed_bitmap.resize(key_count);
    state_value.key_transitions.resize(key_count);
    node.state = std::move(state_value);

    AOAHID_CHECK(aoahid_keyboard_key(&node, 0x06U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_keyboard_key(&node, 0x06U, 0U) == AOAHID_ERR_BUSY);
    std::array<std::uint8_t, 16U> report{};
    std::size_t length = 0U;
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    const auto* key = field(spec, aoa::hid::FieldSemantic::key_bitmap, 2U);
    AOAHID_CHECK(key != nullptr && extract(report.data(), *key) == 1U);
    aoa::detail::transfer_complete(&node, AOAHID_OK, 0);

    AOAHID_CHECK(aoahid_keyboard_key(&node, 0x06U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_keyboard_key(&node, 0x06U, 1U) == AOAHID_ERR_BUSY);
    report.fill(0xFFU);
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    AOAHID_CHECK(key != nullptr && extract(report.data(), *key) == 0U);
    aoa::detail::transfer_complete(&node, AOAHID_ERR_IO, 1);
    AOAHID_CHECK(aoahid_keyboard_key(&node, 0x06U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_keyboard_key(&node, 0x06U, 0U) == AOAHID_ERR_BUSY);
    aoa::detail::transfer_complete(&node, AOAHID_OK, 0);
    AOAHID_CHECK(aoahid_keyboard_key(&node, 0x06U, 0U) == AOAHID_OK);
    aoahid_spec_release(spec);
}

void test_send_time_report_validation() {
    aoahid_keyboard_options options = keyboard_options();
    options.report_id = {1U, 5U, {0U, 0U, 0U}};
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&options, &spec) == AOAHID_OK);
    if (spec == nullptr)
        return;

    aoahid_node node{};
    node.spec = spec;
    aoa::detail::KeyboardState state_value{};
    const std::size_t key_count =
        static_cast<std::size_t>(options.usage_maximum - options.usage_minimum) + 1U;
    state_value.pressed_bitmap.resize(key_count);
    state_value.key_transitions.resize(key_count);
    node.state = std::move(state_value);
    AOAHID_CHECK(aoahid_keyboard_key(&node, 0x04U, 1U) == AOAHID_OK);
    std::array<std::uint8_t, 64U> report{};
    std::size_t length = 0U;
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoa::detail::validate_serialized_report(spec, report.data(), length) == AOAHID_OK);
    AOAHID_CHECK(aoa::detail::validate_serialized_report(spec, report.data(), length - 1U) ==
                 AOAHID_ERR_INTERNAL);

    report[0] = 4U;
    AOAHID_CHECK(aoa::detail::validate_serialized_report(spec, report.data(), length) ==
                 AOAHID_ERR_INTERNAL);
    report[0] = 5U;
    const auto* padding = field(spec, aoa::hid::FieldSemantic::constant_padding);
    AOAHID_CHECK(padding != nullptr);
    if (padding != nullptr) {
        report[padding->bit_offset / 8U] = static_cast<std::uint8_t>(
            report[padding->bit_offset / 8U] | (1U << (padding->bit_offset & 7U)));
        AOAHID_CHECK(aoa::detail::validate_serialized_report(spec, report.data(), length) ==
                     AOAHID_ERR_INTERNAL);
    }
    aoahid_spec_release(spec);
}

} // namespace profile_test
