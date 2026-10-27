// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Emits compiler-observed C ABI sizes and offsets for foreign-binding tests;
 * it does not validate protocol behavior. */
#include "aoahid.h"

#include <stddef.h>
#include <stdio.h>

#define AOAHID_SIZE(type) printf(#type ".size=%zu\n", sizeof(type))
#define AOAHID_OFFSET(type, field) printf(#type "." #field "=%zu\n", offsetof(type, field))

#define AOAHID_BEGIN(type) AOAHID_SIZE(type)
#define AOAHID_FIELD(type, field) AOAHID_OFFSET(type, field)
#define AOAHID_CONSTANT(name) printf(#name "=%d\n", (int)(name))

int main(void) {
    AOAHID_CONSTANT(AOAHID_VERSION_MAJOR);
    AOAHID_CONSTANT(AOAHID_VERSION_MINOR);
    AOAHID_CONSTANT(AOAHID_VERSION_PATCH);
    AOAHID_CONSTANT(AOAHID_OK);
    AOAHID_CONSTANT(AOAHID_ERR_PARAM);
    AOAHID_CONSTANT(AOAHID_ERR_UNSET_FIELD);
    AOAHID_CONSTANT(AOAHID_ERR_UNSUPPORTED);
    AOAHID_CONSTANT(AOAHID_ERR_NOT_AOA);
    AOAHID_CONSTANT(AOAHID_ERR_ACCESS);
    AOAHID_CONSTANT(AOAHID_ERR_BUSY);
    AOAHID_CONSTANT(AOAHID_ERR_NO_DEVICE);
    AOAHID_CONSTANT(AOAHID_ERR_STALL);
    AOAHID_CONSTANT(AOAHID_ERR_TIMEOUT);
    AOAHID_CONSTANT(AOAHID_ERR_SHORT_TRANSFER);
    AOAHID_CONSTANT(AOAHID_ERR_DESCRIPTOR_REJECTED);
    AOAHID_CONSTANT(AOAHID_ERR_IO);
    AOAHID_CONSTANT(AOAHID_ERR_OVERFLOW);
    AOAHID_CONSTANT(AOAHID_CLOSE_PENDING);
    AOAHID_CONSTANT(AOAHID_ERR_INTERNAL);
    AOAHID_CONSTANT(AOAHID_EVENT_CALLER_POLL);
    AOAHID_CONSTANT(AOAHID_EVENT_INTERNAL_THREAD);
    AOAHID_CONSTANT(AOAHID_CHANNEL_READ_STREAM);
    AOAHID_CONSTANT(AOAHID_CHANNEL_READ_REQUEST);
    AOAHID_CONSTANT(AOAHID_INTERFACE_CLAIM_NONE);
    AOAHID_CONSTANT(AOAHID_INTERFACE_CLAIM_EXPLICIT);
    AOAHID_CONSTANT(AOAHID_LOG_DISABLED);
    AOAHID_CONSTANT(AOAHID_LOG_ERROR);
    AOAHID_CONSTANT(AOAHID_LOG_INFO);
    AOAHID_CONSTANT(AOAHID_LOG_TRACE);
    AOAHID_CONSTANT(AOAHID_PROFILE_KEYBOARD);
    AOAHID_CONSTANT(AOAHID_PROFILE_MOUSE);
    AOAHID_CONSTANT(AOAHID_PROFILE_TOGGLE);
    AOAHID_CONSTANT(AOAHID_PROFILE_GAMEPAD);
    AOAHID_CONSTANT(AOAHID_PROFILE_TOUCHSCREEN);
    AOAHID_CONSTANT(AOAHID_PROFILE_PEN);
    AOAHID_CONSTANT(AOAHID_PROFILE_RAW);
    AOAHID_CONSTANT(AOAHID_PROFILE_TOUCHPAD);
    AOAHID_CONSTANT(AOAHID_PROFILE_DIGITIZER);
    AOAHID_CONSTANT(AOAHID_ANDROID_PORTABLE_CANDIDATE);
    AOAHID_CONSTANT(AOAHID_ANDROID_CONDITIONAL);
    AOAHID_CONSTANT(AOAHID_ANDROID_CUSTOM_SYSTEM_ONLY);
    AOAHID_CONSTANT(AOAHID_ANDROID_UNSUPPORTED);
    AOAHID_CONSTANT(AOAHID_ANDROID_UNKNOWN);
    AOAHID_CONSTANT(AOAHID_AXIS_X);
    AOAHID_CONSTANT(AOAHID_AXIS_Y);
    AOAHID_CONSTANT(AOAHID_AXIS_Z);
    AOAHID_CONSTANT(AOAHID_AXIS_RX);
    AOAHID_CONSTANT(AOAHID_AXIS_RY);
    AOAHID_CONSTANT(AOAHID_AXIS_RZ);
    AOAHID_CONSTANT(AOAHID_AXIS_SLIDER);
    AOAHID_CONSTANT(AOAHID_AXIS_SIMULATION_ACCELERATOR);
    AOAHID_CONSTANT(AOAHID_AXIS_SIMULATION_BRAKE);
    AOAHID_CONSTANT(AOAHID_AXIS_SIMULATION_STEERING);
    AOAHID_CONSTANT(AOAHID_AXIS_DIAL);
    AOAHID_CONSTANT(AOAHID_AXIS_WHEEL);
    AOAHID_CONSTANT(AOAHID_AXIS_SIMULATION_RUDDER);
    AOAHID_CONSTANT(AOAHID_AXIS_SIMULATION_THROTTLE);
    AOAHID_CONSTANT(AOAHID_DPAD_NONE);
    AOAHID_CONSTANT(AOAHID_DPAD_HAT);
    AOAHID_CONSTANT(AOAHID_DPAD_BUTTONS);
    AOAHID_CONSTANT(AOAHID_USAGE_SELECTOR_BITMAP);
    AOAHID_CONSTANT(AOAHID_USAGE_ON_OFF_TOGGLE);
    AOAHID_CONSTANT(AOAHID_USAGE_ON_OFF_MAINTAINED);
    AOAHID_CONSTANT(AOAHID_USAGE_MOMENTARY);
    AOAHID_CONSTANT(AOAHID_USAGE_ONE_SHOT);
    AOAHID_CONSTANT(AOAHID_USAGE_RETRIGGER);
    AOAHID_CONSTANT(AOAHID_USAGE_ON_OFF_PAIR);
    AOAHID_CONSTANT(AOAHID_USAGE_LINEAR);
    AOAHID_CONSTANT(AOAHID_USAGE_DYNAMIC_VALUE);
    AOAHID_CONSTANT(AOAHID_USAGE_NAMED_ARRAY);

    AOAHID_BEGIN(aoahid_error_detail);
    AOAHID_FIELD(aoahid_error_detail, code);
    AOAHID_FIELD(aoahid_error_detail, field);
    AOAHID_FIELD(aoahid_error_detail, reason);
    AOAHID_FIELD(aoahid_error_detail, transport_status);
    AOAHID_FIELD(aoahid_error_detail, aoa_request);
    AOAHID_FIELD(aoahid_error_detail, node_id);
    AOAHID_FIELD(aoahid_error_detail, report_id);
    AOAHID_FIELD(aoahid_error_detail, offset);
    AOAHID_FIELD(aoahid_error_detail, length);

    AOAHID_BEGIN(aoahid_context_options);
    AOAHID_FIELD(aoahid_context_options, struct_size);
    AOAHID_FIELD(aoahid_context_options, reserved);
    AOAHID_FIELD(aoahid_context_options, event_mode);
    AOAHID_FIELD(aoahid_context_options, log_level);
    AOAHID_FIELD(aoahid_context_options, log_sink);
    AOAHID_FIELD(aoahid_context_options, log_user);

    AOAHID_BEGIN(aoahid_device_info);
    AOAHID_FIELD(aoahid_device_info, bus_number);
    AOAHID_FIELD(aoahid_device_info, device_address);
    AOAHID_FIELD(aoahid_device_info, port_path);
    AOAHID_FIELD(aoahid_device_info, port_path_length);
    AOAHID_FIELD(aoahid_device_info, vendor_id);
    AOAHID_FIELD(aoahid_device_info, product_id);
    AOAHID_FIELD(aoahid_device_info, protocol_version);
    AOAHID_FIELD(aoahid_device_info, serial);
    AOAHID_FIELD(aoahid_device_info, product);

    AOAHID_BEGIN(aoahid_aoa_strings);
    AOAHID_FIELD(aoahid_aoa_strings, manufacturer);
    AOAHID_FIELD(aoahid_aoa_strings, model);
    AOAHID_FIELD(aoahid_aoa_strings, description);
    AOAHID_FIELD(aoahid_aoa_strings, version);
    AOAHID_FIELD(aoahid_aoa_strings, uri);
    AOAHID_FIELD(aoahid_aoa_strings, serial);

    AOAHID_BEGIN(aoahid_device_options);
    AOAHID_FIELD(aoahid_device_options, struct_size);
    AOAHID_FIELD(aoahid_device_options, reserved);
    AOAHID_FIELD(aoahid_device_options, descriptor_fragment_bytes);
    AOAHID_FIELD(aoahid_device_options, transfer_pool_slots);
    AOAHID_FIELD(aoahid_device_options, maximum_report_bytes);
    AOAHID_FIELD(aoahid_device_options, aoa_descriptor_wire_policy_bytes);
    AOAHID_FIELD(aoahid_device_options, linux_descriptor_policy_bytes);
    AOAHID_FIELD(aoahid_device_options, linux_hid_fields_per_report_policy);
    AOAHID_FIELD(aoahid_device_options, linux_hid_global_stack_depth_policy);
    AOAHID_FIELD(aoahid_device_options, linux_hid_usages_policy);
    AOAHID_FIELD(aoahid_device_options, linux_hid_report_data_policy_bits);
    AOAHID_FIELD(aoahid_device_options, linux_hid_report_size_policy_bits);
    AOAHID_FIELD(aoahid_device_options, target_ep0_data_policy_bytes);
    AOAHID_FIELD(aoahid_device_options, host_control_buffer_policy_bytes);
    AOAHID_FIELD(aoahid_device_options, interface_claim_policy);
    AOAHID_FIELD(aoahid_device_options, interface_number);
    AOAHID_FIELD(aoahid_device_options, validate_reports);

    AOAHID_BEGIN(aoahid_accessory_options);
    AOAHID_FIELD(aoahid_accessory_options, struct_size);
    AOAHID_FIELD(aoahid_accessory_options, reserved);
    AOAHID_FIELD(aoahid_accessory_options, strings);
    AOAHID_FIELD(aoahid_accessory_options, control_timeout_ms);

    AOAHID_BEGIN(aoahid_channel_options);
    AOAHID_FIELD(aoahid_channel_options, struct_size);
    AOAHID_FIELD(aoahid_channel_options, reserved);
    AOAHID_FIELD(aoahid_channel_options, interface_class);
    AOAHID_FIELD(aoahid_channel_options, interface_subclass);
    AOAHID_FIELD(aoahid_channel_options, interface_protocol);
    AOAHID_FIELD(aoahid_channel_options, reserved8);
    AOAHID_FIELD(aoahid_channel_options, in_transfers);
    AOAHID_FIELD(aoahid_channel_options, out_transfers);
    AOAHID_FIELD(aoahid_channel_options, transfer_bytes);
    AOAHID_FIELD(aoahid_channel_options, zero_length_termination);
    AOAHID_FIELD(aoahid_channel_options, read_mode);

    AOAHID_BEGIN(aoahid_node_options);
    AOAHID_FIELD(aoahid_node_options, struct_size);
    AOAHID_FIELD(aoahid_node_options, reserved);
    AOAHID_FIELD(aoahid_node_options, has_reserved_slots);
    AOAHID_FIELD(aoahid_node_options, reserved_slots);

    AOAHID_BEGIN(aoahid_physical_properties);
    AOAHID_FIELD(aoahid_physical_properties, enabled);
    AOAHID_FIELD(aoahid_physical_properties, minimum);
    AOAHID_FIELD(aoahid_physical_properties, maximum);
    AOAHID_FIELD(aoahid_physical_properties, unit_exponent);
    AOAHID_FIELD(aoahid_physical_properties, unit);

    AOAHID_BEGIN(aoahid_integer_field);
    AOAHID_FIELD(aoahid_integer_field, logical_minimum);
    AOAHID_FIELD(aoahid_integer_field, logical_maximum);
    AOAHID_FIELD(aoahid_integer_field, bit_width);
    AOAHID_FIELD(aoahid_integer_field, physical);

    AOAHID_BEGIN(aoahid_report_id_option);
    AOAHID_FIELD(aoahid_report_id_option, enabled);
    AOAHID_FIELD(aoahid_report_id_option, value);
    AOAHID_FIELD(aoahid_report_id_option, reserved8);

    AOAHID_BEGIN(aoahid_battery_report);
    AOAHID_FIELD(aoahid_battery_report, report_id);
    AOAHID_FIELD(aoahid_battery_report, strength);

    AOAHID_BEGIN(aoahid_keyboard_options);
    AOAHID_FIELD(aoahid_keyboard_options, struct_size);
    AOAHID_FIELD(aoahid_keyboard_options, reserved);
    AOAHID_FIELD(aoahid_keyboard_options, report_id);
    AOAHID_FIELD(aoahid_keyboard_options, usage_minimum);
    AOAHID_FIELD(aoahid_keyboard_options, usage_maximum);
    AOAHID_FIELD(aoahid_keyboard_options, enable_battery);
    AOAHID_FIELD(aoahid_keyboard_options, battery);

    AOAHID_BEGIN(aoahid_mouse_options);
    AOAHID_FIELD(aoahid_mouse_options, struct_size);
    AOAHID_FIELD(aoahid_mouse_options, reserved);
    AOAHID_FIELD(aoahid_mouse_options, report_id);
    AOAHID_FIELD(aoahid_mouse_options, button_count);
    AOAHID_FIELD(aoahid_mouse_options, x);
    AOAHID_FIELD(aoahid_mouse_options, y);
    AOAHID_FIELD(aoahid_mouse_options, enable_wheel);
    AOAHID_FIELD(aoahid_mouse_options, wheel);
    AOAHID_FIELD(aoahid_mouse_options, enable_pan);
    AOAHID_FIELD(aoahid_mouse_options, pan);
    AOAHID_FIELD(aoahid_mouse_options, enable_battery);
    AOAHID_FIELD(aoahid_mouse_options, battery);

    AOAHID_BEGIN(aoahid_toggle_options);
    AOAHID_FIELD(aoahid_toggle_options, struct_size);
    AOAHID_FIELD(aoahid_toggle_options, reserved);
    AOAHID_FIELD(aoahid_toggle_options, report_id);
    AOAHID_FIELD(aoahid_toggle_options, application_page);
    AOAHID_FIELD(aoahid_toggle_options, application_usage);
    AOAHID_FIELD(aoahid_toggle_options, field_page);
    AOAHID_FIELD(aoahid_toggle_options, reserved16);
    AOAHID_FIELD(aoahid_toggle_options, allowed_usages);
    AOAHID_FIELD(aoahid_toggle_options, allowed_usage_count);
    AOAHID_FIELD(aoahid_toggle_options, usage_semantics);
    AOAHID_FIELD(aoahid_toggle_options, expected_linux_event_types);
    AOAHID_FIELD(aoahid_toggle_options, expected_linux_codes);
    AOAHID_FIELD(aoahid_toggle_options, enable_battery);
    AOAHID_FIELD(aoahid_toggle_options, battery);

    AOAHID_BEGIN(aoahid_gamepad_axis_option);
    AOAHID_FIELD(aoahid_gamepad_axis_option, role);
    AOAHID_FIELD(aoahid_gamepad_axis_option, usage_page);
    AOAHID_FIELD(aoahid_gamepad_axis_option, usage);
    AOAHID_FIELD(aoahid_gamepad_axis_option, value);
    AOAHID_FIELD(aoahid_gamepad_axis_option, neutral_value);
    AOAHID_FIELD(aoahid_gamepad_axis_option, expected_linux_code);
    AOAHID_FIELD(aoahid_gamepad_axis_option, expected_android_axis);

    AOAHID_BEGIN(aoahid_gamepad_options);
    AOAHID_FIELD(aoahid_gamepad_options, struct_size);
    AOAHID_FIELD(aoahid_gamepad_options, reserved);
    AOAHID_FIELD(aoahid_gamepad_options, report_id);
    AOAHID_FIELD(aoahid_gamepad_options, axes);
    AOAHID_FIELD(aoahid_gamepad_options, axis_count);
    AOAHID_FIELD(aoahid_gamepad_options, button_count);
    AOAHID_FIELD(aoahid_gamepad_options, button_usage_minimum);
    AOAHID_FIELD(aoahid_gamepad_options, dpad_representation);
    AOAHID_FIELD(aoahid_gamepad_options, hat_logical_minimum);
    AOAHID_FIELD(aoahid_gamepad_options, hat_logical_maximum);
    AOAHID_FIELD(aoahid_gamepad_options, hat_bit_width);
    AOAHID_FIELD(aoahid_gamepad_options, enable_battery);
    AOAHID_FIELD(aoahid_gamepad_options, battery);

    AOAHID_BEGIN(aoahid_touchscreen_options);
    AOAHID_FIELD(aoahid_touchscreen_options, struct_size);
    AOAHID_FIELD(aoahid_touchscreen_options, reserved);
    AOAHID_FIELD(aoahid_touchscreen_options, report_id);
    AOAHID_FIELD(aoahid_touchscreen_options, maximum_contacts);
    AOAHID_FIELD(aoahid_touchscreen_options, contacts_per_report);
    AOAHID_FIELD(aoahid_touchscreen_options, contact_id);
    AOAHID_FIELD(aoahid_touchscreen_options, x);
    AOAHID_FIELD(aoahid_touchscreen_options, y);
    AOAHID_FIELD(aoahid_touchscreen_options, contact_count);
    AOAHID_FIELD(aoahid_touchscreen_options, enable_pressure);
    AOAHID_FIELD(aoahid_touchscreen_options, pressure);
    AOAHID_FIELD(aoahid_touchscreen_options, enable_width);
    AOAHID_FIELD(aoahid_touchscreen_options, width);
    AOAHID_FIELD(aoahid_touchscreen_options, enable_height);
    AOAHID_FIELD(aoahid_touchscreen_options, height);
    AOAHID_FIELD(aoahid_touchscreen_options, enable_azimuth);
    AOAHID_FIELD(aoahid_touchscreen_options, azimuth);
    AOAHID_FIELD(aoahid_touchscreen_options, enable_scan_time);
    AOAHID_FIELD(aoahid_touchscreen_options, scan_time);
    AOAHID_FIELD(aoahid_touchscreen_options, scan_time_unit_100us);
    AOAHID_FIELD(aoahid_touchscreen_options, enable_contact_count_maximum_feature_declaration);
    AOAHID_FIELD(aoahid_touchscreen_options, enable_multi_packet_frames);
    AOAHID_FIELD(aoahid_touchscreen_options, enable_battery);
    AOAHID_FIELD(aoahid_touchscreen_options, battery);

    AOAHID_BEGIN(aoahid_touchpad_options);
    AOAHID_FIELD(aoahid_touchpad_options, struct_size);
    AOAHID_FIELD(aoahid_touchpad_options, reserved);
    AOAHID_FIELD(aoahid_touchpad_options, report_id);
    AOAHID_FIELD(aoahid_touchpad_options, maximum_contacts);
    AOAHID_FIELD(aoahid_touchpad_options, contacts_per_report);
    AOAHID_FIELD(aoahid_touchpad_options, contact_id);
    AOAHID_FIELD(aoahid_touchpad_options, x);
    AOAHID_FIELD(aoahid_touchpad_options, y);
    AOAHID_FIELD(aoahid_touchpad_options, contact_count);
    AOAHID_FIELD(aoahid_touchpad_options, enable_pressure);
    AOAHID_FIELD(aoahid_touchpad_options, pressure);
    AOAHID_FIELD(aoahid_touchpad_options, enable_width);
    AOAHID_FIELD(aoahid_touchpad_options, width);
    AOAHID_FIELD(aoahid_touchpad_options, enable_height);
    AOAHID_FIELD(aoahid_touchpad_options, height);
    AOAHID_FIELD(aoahid_touchpad_options, enable_azimuth);
    AOAHID_FIELD(aoahid_touchpad_options, azimuth);
    AOAHID_FIELD(aoahid_touchpad_options, enable_scan_time);
    AOAHID_FIELD(aoahid_touchpad_options, scan_time);
    AOAHID_FIELD(aoahid_touchpad_options, scan_time_unit_100us);
    AOAHID_FIELD(aoahid_touchpad_options, enable_contact_count_maximum_feature_declaration);
    AOAHID_FIELD(aoahid_touchpad_options, enable_multi_packet_frames);
    AOAHID_FIELD(aoahid_touchpad_options, button_count);
    AOAHID_FIELD(aoahid_touchpad_options, enable_battery);
    AOAHID_FIELD(aoahid_touchpad_options, battery);

    AOAHID_BEGIN(aoahid_pen_options);
    AOAHID_FIELD(aoahid_pen_options, struct_size);
    AOAHID_FIELD(aoahid_pen_options, reserved);
    AOAHID_FIELD(aoahid_pen_options, report_id);
    AOAHID_FIELD(aoahid_pen_options, x);
    AOAHID_FIELD(aoahid_pen_options, y);
    AOAHID_FIELD(aoahid_pen_options, enable_pressure);
    AOAHID_FIELD(aoahid_pen_options, pressure);
    AOAHID_FIELD(aoahid_pen_options, enable_tilt);
    AOAHID_FIELD(aoahid_pen_options, tilt_x);
    AOAHID_FIELD(aoahid_pen_options, tilt_y);
    AOAHID_FIELD(aoahid_pen_options, enable_twist_target_specific);
    AOAHID_FIELD(aoahid_pen_options, twist);
    AOAHID_FIELD(aoahid_pen_options, barrel_usages);
    AOAHID_FIELD(aoahid_pen_options, barrel_usage_count);
    AOAHID_FIELD(aoahid_pen_options, enable_eraser);
    AOAHID_FIELD(aoahid_pen_options, enable_hover);
    AOAHID_FIELD(aoahid_pen_options, enable_battery);
    AOAHID_FIELD(aoahid_pen_options, battery);

    AOAHID_BEGIN(aoahid_digitizer_options);
    AOAHID_FIELD(aoahid_digitizer_options, struct_size);
    AOAHID_FIELD(aoahid_digitizer_options, reserved);
    AOAHID_FIELD(aoahid_digitizer_options, report_id);
    AOAHID_FIELD(aoahid_digitizer_options, x);
    AOAHID_FIELD(aoahid_digitizer_options, y);
    AOAHID_FIELD(aoahid_digitizer_options, enable_pressure);
    AOAHID_FIELD(aoahid_digitizer_options, pressure);
    AOAHID_FIELD(aoahid_digitizer_options, enable_tilt);
    AOAHID_FIELD(aoahid_digitizer_options, tilt_x);
    AOAHID_FIELD(aoahid_digitizer_options, tilt_y);
    AOAHID_FIELD(aoahid_digitizer_options, enable_twist_target_specific);
    AOAHID_FIELD(aoahid_digitizer_options, twist);
    AOAHID_FIELD(aoahid_digitizer_options, barrel_usages);
    AOAHID_FIELD(aoahid_digitizer_options, barrel_usage_count);
    AOAHID_FIELD(aoahid_digitizer_options, enable_eraser);
    AOAHID_FIELD(aoahid_digitizer_options, enable_hover);
    AOAHID_FIELD(aoahid_digitizer_options, enable_battery);
    AOAHID_FIELD(aoahid_digitizer_options, battery);

    AOAHID_BEGIN(aoahid_raw_report);
    AOAHID_FIELD(aoahid_raw_report, report_id);
    AOAHID_FIELD(aoahid_raw_report, wire_length);

    AOAHID_BEGIN(aoahid_raw_options);
    AOAHID_FIELD(aoahid_raw_options, struct_size);
    AOAHID_FIELD(aoahid_raw_options, reserved);
    AOAHID_FIELD(aoahid_raw_options, descriptor);
    AOAHID_FIELD(aoahid_raw_options, descriptor_length);
    AOAHID_FIELD(aoahid_raw_options, reports);
    AOAHID_FIELD(aoahid_raw_options, report_count);
    AOAHID_FIELD(aoahid_raw_options, acknowledges_no_android_support);
    AOAHID_FIELD(aoahid_raw_options, requires_output);
    AOAHID_FIELD(aoahid_raw_options, requires_feature_response);

    AOAHID_BEGIN(aoahid_report_capability);
    AOAHID_FIELD(aoahid_report_capability, report_id);
    AOAHID_FIELD(aoahid_report_capability, wire_length);

    AOAHID_BEGIN(aoahid_capability_manifest);
    AOAHID_FIELD(aoahid_capability_manifest, struct_size);
    AOAHID_FIELD(aoahid_capability_manifest, reserved);
    AOAHID_FIELD(aoahid_capability_manifest, input_supported);
    AOAHID_FIELD(aoahid_capability_manifest, output_supported);
    AOAHID_FIELD(aoahid_capability_manifest, feature_transport_supported);
    AOAHID_FIELD(aoahid_capability_manifest, android_status);
    AOAHID_FIELD(aoahid_capability_manifest, profile_kind);
    AOAHID_FIELD(aoahid_capability_manifest, reports);
    AOAHID_FIELD(aoahid_capability_manifest, report_count);
    AOAHID_FIELD(aoahid_capability_manifest, descriptor_length);

    AOAHID_BEGIN(aoahid_touchscreen_extra);
    AOAHID_FIELD(aoahid_touchscreen_extra, pressure);
    AOAHID_FIELD(aoahid_touchscreen_extra, width);
    AOAHID_FIELD(aoahid_touchscreen_extra, height);
    AOAHID_FIELD(aoahid_touchscreen_extra, azimuth);

    AOAHID_BEGIN(aoahid_touchpad_extra);
    AOAHID_FIELD(aoahid_touchpad_extra, pressure);
    AOAHID_FIELD(aoahid_touchpad_extra, width);
    AOAHID_FIELD(aoahid_touchpad_extra, height);
    AOAHID_FIELD(aoahid_touchpad_extra, azimuth);

    AOAHID_BEGIN(aoahid_pen_sample);
    AOAHID_FIELD(aoahid_pen_sample, in_range);
    AOAHID_FIELD(aoahid_pen_sample, tip);
    AOAHID_FIELD(aoahid_pen_sample, eraser);
    AOAHID_FIELD(aoahid_pen_sample, barrel_buttons);
    AOAHID_FIELD(aoahid_pen_sample, x);
    AOAHID_FIELD(aoahid_pen_sample, y);
    AOAHID_FIELD(aoahid_pen_sample, pressure);
    AOAHID_FIELD(aoahid_pen_sample, tilt_x);
    AOAHID_FIELD(aoahid_pen_sample, tilt_y);
    AOAHID_FIELD(aoahid_pen_sample, twist);

    AOAHID_BEGIN(aoahid_digitizer_sample);
    AOAHID_FIELD(aoahid_digitizer_sample, in_range);
    AOAHID_FIELD(aoahid_digitizer_sample, tip);
    AOAHID_FIELD(aoahid_digitizer_sample, eraser);
    AOAHID_FIELD(aoahid_digitizer_sample, barrel_buttons);
    AOAHID_FIELD(aoahid_digitizer_sample, x);
    AOAHID_FIELD(aoahid_digitizer_sample, y);
    AOAHID_FIELD(aoahid_digitizer_sample, pressure);
    AOAHID_FIELD(aoahid_digitizer_sample, tilt_x);
    AOAHID_FIELD(aoahid_digitizer_sample, tilt_y);
    AOAHID_FIELD(aoahid_digitizer_sample, twist);
    return 0;
}
