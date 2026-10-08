// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
#pragma once

/* Every function of the C ABI in aoahid.h, forwarded unchanged under namespace
 * aoahid without the aoahid_ prefix. Types, arguments, return values and the
 * documentation are those of aoahid.h; this header adds nothing else. */

#include "aoahid.h"

namespace aoahid {

[[nodiscard]] inline const aoahid_error_detail* last_error() noexcept {
    return aoahid_last_error();
}
[[nodiscard]] inline const char* result_name(aoahid_result result) noexcept {
    return aoahid_result_name(result);
}
[[nodiscard]] inline uint32_t version() noexcept { return aoahid_version(); }
[[nodiscard]] inline aoahid_result context_create(const aoahid_context_options* options,
                                                  aoahid_context** out_context) noexcept {
    return aoahid_context_create(options, out_context);
}
[[nodiscard]] inline aoahid_result context_poll(aoahid_context* context,
                                                uint32_t timeout_ms) noexcept {
    return aoahid_context_poll(context, timeout_ms);
}
[[nodiscard]] inline aoahid_result context_destroy(aoahid_context* context) noexcept {
    return aoahid_context_destroy(context);
}
[[nodiscard]] inline aoahid_result context_destroy_blocking(aoahid_context* context,
                                                            uint32_t timeout_ms) noexcept {
    return aoahid_context_destroy_blocking(context, timeout_ms);
}
[[nodiscard]] inline aoahid_result discover(aoahid_context* context, uint32_t control_timeout_ms,
                                            aoahid_discovery** out_discovery) noexcept {
    return aoahid_discover(context, control_timeout_ms, out_discovery);
}
[[nodiscard]] inline size_t discovery_count(const aoahid_discovery* discovery) noexcept {
    return aoahid_discovery_count(discovery);
}
[[nodiscard]] inline const aoahid_device_info* discovery_get(const aoahid_discovery* discovery,
                                                             size_t index) noexcept {
    return aoahid_discovery_get(discovery, index);
}
inline void discovery_destroy(aoahid_discovery* discovery) noexcept {
    aoahid_discovery_destroy(discovery);
}
[[nodiscard]] inline aoahid_result
accessory_start(aoahid_context* context, const aoahid_device_info* selected,
                const aoahid_accessory_options* options) noexcept {
    return aoahid_accessory_start(context, selected, options);
}
[[nodiscard]] inline aoahid_result device_open(aoahid_context* context,
                                               const aoahid_device_info* selected,
                                               const aoahid_device_options* options,
                                               aoahid_device** out_device) noexcept {
    return aoahid_device_open(context, selected, options, out_device);
}
[[nodiscard]] inline aoahid_result device_close(aoahid_device* device) noexcept {
    return aoahid_device_close(device);
}
[[nodiscard]] inline aoahid_result device_latched_error(aoahid_device* device) noexcept {
    return aoahid_device_latched_error(device);
}
[[nodiscard]] inline aoahid_result channel_open(aoahid_device* device,
                                                const aoahid_channel_options* options,
                                                aoahid_channel** out_channel) noexcept {
    return aoahid_channel_open(device, options, out_channel);
}
[[nodiscard]] inline aoahid_result channel_close(aoahid_channel* channel) noexcept {
    return aoahid_channel_close(channel);
}
[[nodiscard]] inline aoahid_result channel_write(aoahid_channel* channel, const uint8_t* data,
                                                 size_t length, size_t* out_written,
                                                 uint32_t timeout_ms) noexcept {
    return aoahid_channel_write(channel, data, length, out_written, timeout_ms);
}
[[nodiscard]] inline aoahid_result channel_read(aoahid_channel* channel, uint8_t* buffer,
                                                size_t capacity, size_t* out_received,
                                                uint32_t timeout_ms) noexcept {
    return aoahid_channel_read(channel, buffer, capacity, out_received, timeout_ms);
}
[[nodiscard]] inline aoahid_result spec_create_keyboard(const aoahid_keyboard_options* options,
                                                        aoahid_spec** out_spec) noexcept {
    return aoahid_spec_create_keyboard(options, out_spec);
}
[[nodiscard]] inline aoahid_result spec_create_mouse(const aoahid_mouse_options* options,
                                                     aoahid_spec** out_spec) noexcept {
    return aoahid_spec_create_mouse(options, out_spec);
}
[[nodiscard]] inline aoahid_result spec_create_toggle(const aoahid_toggle_options* options,
                                                      aoahid_spec** out_spec) noexcept {
    return aoahid_spec_create_toggle(options, out_spec);
}
[[nodiscard]] inline aoahid_result spec_create_gamepad(const aoahid_gamepad_options* options,
                                                       aoahid_spec** out_spec) noexcept {
    return aoahid_spec_create_gamepad(options, out_spec);
}
[[nodiscard]] inline aoahid_result
spec_create_touchscreen(const aoahid_touchscreen_options* options,
                        aoahid_spec** out_spec) noexcept {
    return aoahid_spec_create_touchscreen(options, out_spec);
}
[[nodiscard]] inline aoahid_result spec_create_touchpad(const aoahid_touchpad_options* options,
                                                        aoahid_spec** out_spec) noexcept {
    return aoahid_spec_create_touchpad(options, out_spec);
}
[[nodiscard]] inline aoahid_result spec_create_pen(const aoahid_pen_options* options,
                                                   aoahid_spec** out_spec) noexcept {
    return aoahid_spec_create_pen(options, out_spec);
}
[[nodiscard]] inline aoahid_result spec_create_battery(const aoahid_battery_options* options,
                                                       aoahid_spec** out_spec) noexcept {
    return aoahid_spec_create_battery(options, out_spec);
}
[[nodiscard]] inline aoahid_result spec_create_raw(const aoahid_raw_options* options,
                                                   aoahid_spec** out_spec) noexcept {
    return aoahid_spec_create_raw(options, out_spec);
}
inline void spec_retain(aoahid_spec* spec) noexcept { aoahid_spec_retain(spec); }
inline void spec_release(aoahid_spec* spec) noexcept { aoahid_spec_release(spec); }
[[nodiscard]] inline aoahid_result spec_descriptor(const aoahid_spec* spec, const uint8_t** bytes,
                                                   size_t* length) noexcept {
    return aoahid_spec_descriptor(spec, bytes, length);
}
[[nodiscard]] inline aoahid_result spec_manifest(const aoahid_spec* spec,
                                                 aoahid_capability_manifest* manifest) noexcept {
    return aoahid_spec_manifest(spec, manifest);
}
[[nodiscard]] inline aoahid_result node_open(aoahid_device* device, aoahid_spec* spec,
                                             const aoahid_node_options* options,
                                             aoahid_node** out_node) noexcept {
    return aoahid_node_open(device, spec, options, out_node);
}
[[nodiscard]] inline aoahid_result node_close(aoahid_node* node) noexcept {
    return aoahid_node_close(node);
}
[[nodiscard]] inline uint16_t node_hid_id(const aoahid_node* node) noexcept {
    return aoahid_node_hid_id(node);
}
[[nodiscard]] inline aoahid_result node_manifest(const aoahid_node* node,
                                                 aoahid_capability_manifest* manifest) noexcept {
    return aoahid_node_manifest(node, manifest);
}
[[nodiscard]] inline aoahid_result node_submit(aoahid_node* node) noexcept {
    return aoahid_node_submit(node);
}
[[nodiscard]] inline aoahid_result node_submit_blocking(aoahid_node* node,
                                                        uint32_t deadline_ms) noexcept {
    return aoahid_node_submit_blocking(node, deadline_ms);
}
[[nodiscard]] inline aoahid_result kbd(aoahid_node* node, uint16_t usage, uint32_t down) noexcept {
    return aoahid_kbd(node, usage, down);
}
[[nodiscard]] inline aoahid_result mouse_move(aoahid_node* node, int32_t dx, int32_t dy) noexcept {
    return aoahid_mouse_move(node, dx, dy);
}
[[nodiscard]] inline aoahid_result mouse_scroll(aoahid_node* node, int32_t wheel,
                                                int32_t pan) noexcept {
    return aoahid_mouse_scroll(node, wheel, pan);
}
[[nodiscard]] inline aoahid_result mouse_button(aoahid_node* node, uint32_t button,
                                                uint32_t pressed) noexcept {
    return aoahid_mouse_button(node, button, pressed);
}
[[nodiscard]] inline aoahid_result toggle(aoahid_node* node, uint16_t usage,
                                          uint32_t down) noexcept {
    return aoahid_toggle(node, usage, down);
}
[[nodiscard]] inline aoahid_result gamepad_button(aoahid_node* node, uint32_t button,
                                                  uint32_t pressed) noexcept {
    return aoahid_gamepad_button(node, button, pressed);
}
[[nodiscard]] inline aoahid_result gamepad_set_axis(aoahid_node* node, size_t axis_index,
                                                    int32_t value) noexcept {
    return aoahid_gamepad_set_axis(node, axis_index, value);
}
[[nodiscard]] inline aoahid_result dpad(aoahid_node* node, uint32_t up, uint32_t down,
                                        uint32_t right, uint32_t left) noexcept {
    return aoahid_dpad(node, up, down, right, left);
}
[[nodiscard]] inline aoahid_result touch(aoahid_node* node, uint32_t contact_id, uint32_t down,
                                         int32_t x, int32_t y,
                                         const aoahid_touch_extra* extra) noexcept {
    return aoahid_touch(node, contact_id, down, x, y, extra);
}
[[nodiscard]] inline aoahid_result touchpad_button(aoahid_node* node, uint32_t button,
                                                   uint32_t pressed) noexcept {
    return aoahid_touchpad_button(node, button, pressed);
}
[[nodiscard]] inline aoahid_result pen_update(aoahid_node* node,
                                              const aoahid_pen_sample* sample) noexcept {
    return aoahid_pen_update(node, sample);
}
[[nodiscard]] inline aoahid_result pen_depart(aoahid_node* node) noexcept {
    return aoahid_pen_depart(node);
}
[[nodiscard]] inline aoahid_result battery_update(aoahid_node* node, uint32_t has_value,
                                                  int32_t strength) noexcept {
    return aoahid_battery_update(node, has_value, strength);
}
[[nodiscard]] inline aoahid_result raw_submit(aoahid_node* node, const uint8_t* report,
                                              size_t length) noexcept {
    return aoahid_raw_submit(node, report, length);
}

} // namespace aoahid
