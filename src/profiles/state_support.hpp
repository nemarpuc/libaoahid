// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
#pragma once

/*
 * What the profile state machines share: the checks every mutator makes on its Node, the bit
 * writer, the pending-transition bookkeeping, and the wrapper that keeps C++ exceptions inside the
 * C ABI. Mutators do no I/O and allocate no memory after node construction.
 */

#include "api/internal.hpp"
#include "hid/usages.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>

namespace aoa::state_build {

using aoa::detail::ContactPhase;
using aoa::detail::set_error;
using aoa::hid::FieldLayout;
using aoa::hid::FieldSemantic;

inline aoahid_result link_available(const aoahid_node* node, const char* field) noexcept {
    if (node == nullptr) {
        set_error(AOAHID_ERR_PARAM, field, "The node is not attached to a live device.");
        return AOAHID_ERR_PARAM;
    }
    // Pure state-machine tests construct the private node representation
    // without a transport. Every public Node is attached by aoahid_node_open().
    if (node->device == nullptr) {
        return AOAHID_OK;
    }
    if (node->device->transport == nullptr) {
        set_error(AOAHID_ERR_PARAM, field, "The node is not attached to a live device.");
        return AOAHID_ERR_PARAM;
    }
    if (node->device->transport->present()) {
        return AOAHID_OK;
    }
    aoa::transport::ErrorInfo error{};
    static_cast<void>(node->device->transport->latched_error(&error));
    const std::uint16_t failed_report_id =
        error.aoa_request == 57 && error.hid_id == node->hid_id
            ? error.report_id
            : (node->spec != nullptr && node->spec->layout.has_report_id
                   ? node->spec->layout.report_id
                   : 0U);
    set_error(AOAHID_ERR_NO_DEVICE, field, "The device is gone; its node state is frozen.",
              error.native_status, error.aoa_request, node->hid_id, failed_report_id, error.offset,
              error.length);
    aoa::detail::set_error_node(node->node_id);
    return AOAHID_ERR_NO_DEVICE;
}

inline aoahid_result usable(const aoahid_node* node, const aoahid_profile_kind kind,
                            const char* field) noexcept {
    if (node == nullptr || node->closed || node->spec == nullptr || node->spec->kind != kind) {
        set_error(AOAHID_ERR_PARAM, field, "The operation does not match the node profile.");
        return AOAHID_ERR_PARAM;
    }
    const aoahid_result link = link_available(node, field);
    if (link != AOAHID_OK)
        return link;
    if (node->transfer_inflight.load(std::memory_order_acquire)) {
        set_error(AOAHID_ERR_BUSY, field, "The node has one report in flight.");
        return AOAHID_ERR_BUSY;
    }
    return AOAHID_OK;
}

inline bool in_range(const std::int64_t value, const aoahid_integer_field& field) noexcept {
    return value >= field.logical_minimum && value <= field.logical_maximum;
}

inline bool add_checked(std::int64_t* target, const std::int32_t value) noexcept {
    if ((value > 0 && *target > std::numeric_limits<std::int64_t>::max() - value) ||
        (value < 0 && *target < std::numeric_limits<std::int64_t>::min() - value)) {
        return false;
    }
    *target += value;
    return true;
}

inline bool store_bits(std::uint8_t* report, const std::size_t capacity, const FieldLayout& field,
                       const std::int64_t value, const bool allow_null = false) noexcept {
    if (report == nullptr || field.bit_width == 0U || field.bit_width > 32U ||
        field.bit_offset + field.bit_width > capacity * 8U ||
        (allow_null && !field.has_null_state)) {
        return false;
    }
    if (!allow_null && (value < field.logical_minimum || value > field.logical_maximum)) {
        return false;
    }
    std::uint64_t encoded =
        static_cast<std::uint64_t>(value) & ((std::uint64_t{1} << field.bit_width) - 1U);
    std::size_t position = field.bit_offset;
    std::uint32_t remaining = field.bit_width;
    // A field spans at most four bytes, so it is written a byte at a time.
    while (remaining != 0U) {
        const auto shift = static_cast<std::uint32_t>(position & 7U);
        const std::uint32_t taken = std::min(remaining, 8U - shift);
        const std::uint32_t byte_mask = ((1U << taken) - 1U) << shift;
        std::uint8_t& target = report[position / 8U];
        target =
            static_cast<std::uint8_t>((static_cast<std::uint32_t>(target) & ~byte_mask) |
                                      ((static_cast<std::uint32_t>(encoded) << shift) & byte_mask));
        encoded >>= taken;
        position += taken;
        remaining -= taken;
    }
    return true;
}

inline std::int64_t hat_null(const FieldLayout& field) noexcept {
    if (field.is_signed) {
        const std::int64_t minimum = field.bit_width == 32U
                                         ? std::numeric_limits<std::int32_t>::min()
                                         : -(std::int64_t{1} << (field.bit_width - 1U));
        if (minimum < field.logical_minimum) {
            return minimum;
        }
        return field.bit_width == 32U ? std::numeric_limits<std::int32_t>::max()
                                      : (std::int64_t{1} << (field.bit_width - 1U)) - 1;
    }
    const std::int64_t maximum =
        field.bit_width == 32U
            ? static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())
            : (std::int64_t{1} << field.bit_width) - 1;
    if (0 < field.logical_minimum) {
        return 0;
    }
    return maximum;
}

template <typename T> const T* config(const aoahid_node* node) noexcept {
    return std::get_if<T>(&node->spec->config);
}

template <typename T> T* state(aoahid_node* node) noexcept { return std::get_if<T>(&node->state); }

inline aoahid_result pending_transition(const char* field) noexcept {
    set_error(AOAHID_ERR_BUSY, field,
              "An opposite lifecycle transition is pending its first accepted report.");
    return AOAHID_ERR_BUSY;
}

inline bool transition_storage_matches(const std::vector<std::uint8_t>& values,
                                       const std::vector<std::uint8_t>& transitions,
                                       const char* field) noexcept {
    if (values.size() == transitions.size()) {
        return true;
    }
    set_error(AOAHID_ERR_INTERNAL, field,
              "The preallocated lifecycle-transition state does not match the profile layout.");
    return false;
}

inline std::uint32_t pen_barrel_changes(const aoahid_pen_sample& before,
                                        const aoahid_pen_sample& after) noexcept {
    return before.barrel_buttons ^ after.barrel_buttons;
}

inline bool pen_transition_conflicts(const aoa::detail::PenState& pen,
                                     const aoahid_pen_sample& next) noexcept {
    const bool changes_lifecycle =
        pen.desired.in_range != next.in_range || pen.desired.tip != next.tip ||
        pen.desired.eraser != next.eraser || pen_barrel_changes(pen.desired, next) != 0U;
    const bool lifecycle_pending = pen.in_range_transition_pending || pen.tip_transition_pending ||
                                   pen.eraser_transition_pending || pen.barrel_transitions != 0U;
    return changes_lifecycle && lifecycle_pending;
}

inline void record_pen_transitions(aoa::detail::PenState* pen,
                                   const aoahid_pen_sample& next) noexcept {
    pen->in_range_transition_pending =
        pen->in_range_transition_pending || pen->desired.in_range != next.in_range;
    pen->tip_transition_pending = pen->tip_transition_pending || pen->desired.tip != next.tip;
    pen->eraser_transition_pending =
        pen->eraser_transition_pending || pen->desired.eraser != next.eraser;
    pen->barrel_transitions |= pen_barrel_changes(pen->desired, next);
}

inline void consume_lifecycle_transitions(aoahid_node* node) noexcept {
    if (auto* keyboard = state<aoa::detail::KeyboardState>(node); keyboard != nullptr) {
        std::fill(keyboard->key_transitions.begin(), keyboard->key_transitions.end(),
                  std::uint8_t{0});
        keyboard->modifier_transitions = 0U;
    } else if (auto* mouse = state<aoa::detail::MouseState>(node); mouse != nullptr) {
        std::fill(mouse->button_transitions.begin(), mouse->button_transitions.end(),
                  std::uint8_t{0});
    } else if (auto* consumer = state<aoa::detail::ConsumerState>(node); consumer != nullptr) {
        std::fill(consumer->transitions.begin(), consumer->transitions.end(), std::uint8_t{0});
    } else if (auto* gamepad = state<aoa::detail::GamepadState>(node); gamepad != nullptr) {
        std::fill(gamepad->button_transitions.begin(), gamepad->button_transitions.end(),
                  std::uint8_t{0});
        gamepad->hat_transition_pending = false;
    } else if (auto* touch = state<aoa::detail::TouchState>(node); touch != nullptr) {
        for (auto& contact : touch->contacts) {
            contact.lifecycle_transition_pending = false;
        }
        std::fill(touch->button_transitions.begin(), touch->button_transitions.end(),
                  std::uint8_t{0});
    } else if (auto* pen = state<aoa::detail::PenState>(node); pen != nullptr) {
        pen->barrel_transitions = 0U;
        pen->in_range_transition_pending = false;
        pen->tip_transition_pending = false;
        pen->eraser_transition_pending = false;
    }
}

inline std::size_t
collect_touch(const aoa::detail::TouchState& touch,
              std::array<const aoa::detail::ContactState*, 16>* active) noexcept {
    std::size_t count = 0U;
    for (const auto& contact : touch.contacts) {
        if (contact.phase != ContactPhase::none) {
            (*active)[count++] = &contact;
        }
    }
    return count;
}

inline bool validate_contact(const aoa::detail::TouchConfig& config_value,
                             const aoa::detail::TouchContact& contact,
                             const char* operation) noexcept {
    const auto& options = config_value.fields;
    if (contact.contact_id > static_cast<std::uint32_t>(options.contact_id.logical_maximum) ||
        static_cast<std::int64_t>(contact.contact_id) < options.contact_id.logical_minimum ||
        !in_range(contact.x, options.x) || !in_range(contact.y, options.y) ||
        (options.enable_pressure == 1U && !in_range(contact.pressure, options.pressure)) ||
        (options.enable_width == 1U && !in_range(contact.width, options.width)) ||
        (options.enable_height == 1U && !in_range(contact.height, options.height)) ||
        // Linux hid-multitouch.c treats Azimuth samples as [0, Logical
        // Maximum): the maximum is the full-turn endpoint, not a valid sample,
        // even though HID logical extents are otherwise inclusive.
        (options.enable_azimuth == 1U &&
         (contact.azimuth < 0 || contact.azimuth >= options.azimuth.logical_maximum)) ||
        (options.enable_pressure == 0U && contact.pressure != 0) ||
        (options.enable_width == 0U && contact.width != 0) ||
        (options.enable_height == 0U && contact.height != 0) ||
        (options.enable_azimuth == 0U && contact.azimuth != 0)) {
        set_error(AOAHID_ERR_PARAM, operation,
                  "A contact value is outside its declared logical range.");
        return false;
    }
    return true;
}

template <typename Function>
inline aoahid_result abi_state_result([[maybe_unused]] aoahid_node* node, const char* field,
                                      Function&& function) noexcept {
    try {
        AOAHID_SERIALIZED(aoa::detail::serialization_context(node), field);
        return function();
    } catch (const std::bad_alloc&) {
        set_error(AOAHID_ERR_INTERNAL, field,
                  "Memory allocation failed inside the C ABI boundary.");
        return AOAHID_ERR_INTERNAL;
    } catch (...) {
        set_error(AOAHID_ERR_INTERNAL, field,
                  "A C++ exception was contained at the C ABI boundary.");
        return AOAHID_ERR_INTERNAL;
    }
}

} // namespace aoa::state_build
