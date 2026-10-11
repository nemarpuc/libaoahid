// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
//! Compares Rust FFI layout with the compiler-observed C ABI oracle.

use aoahid_sys::*;
use std::collections::BTreeMap;
use std::mem::{offset_of, size_of};
use std::process::Command;

macro_rules! layout {
    ($map:ident, $type:ty, $name:literal, [$($field:ident),* $(,)?]) => {{
        $map.insert(concat!($name, ".size").to_owned(), size_of::<$type>());
        $(
            $map.insert(
                concat!($name, ".", stringify!($field)).to_owned(),
                offset_of!($type, $field),
            );
        )*
    }};
}

macro_rules! constants {
    ($map:ident, [$($name:ident),* $(,)?]) => {{
        $(
            $map.insert(stringify!($name).to_owned(), $name as usize);
        )*
    }};
}

fn rust_constants() -> BTreeMap<String, usize> {
    let mut values = BTreeMap::new();
    constants!(
        values,
        [
            AOAHID_VERSION_MAJOR,
            AOAHID_VERSION_MINOR,
            AOAHID_VERSION_PATCH,
            AOAHID_OK,
            AOAHID_ERR_PARAM,
            AOAHID_ERR_UNSET_FIELD,
            AOAHID_ERR_UNSUPPORTED,
            AOAHID_ERR_NOT_AOA,
            AOAHID_ERR_ACCESS,
            AOAHID_ERR_BUSY,
            AOAHID_ERR_NO_DEVICE,
            AOAHID_ERR_STALL,
            AOAHID_ERR_TIMEOUT,
            AOAHID_ERR_SHORT_TRANSFER,
            AOAHID_ERR_DESCRIPTOR_REJECTED,
            AOAHID_ERR_IO,
            AOAHID_ERR_OVERFLOW,
            AOAHID_CLOSE_PENDING,
            AOAHID_ERR_INTERNAL,
            AOAHID_EVENT_CALLER_POLL,
            AOAHID_EVENT_INTERNAL_THREAD,
            AOAHID_CHANNEL_READ_STREAM,
            AOAHID_CHANNEL_READ_REQUEST,
            AOAHID_INTERFACE_CLAIM_NONE,
            AOAHID_INTERFACE_CLAIM_EXPLICIT,
            AOAHID_LOG_DISABLED,
            AOAHID_LOG_ERROR,
            AOAHID_LOG_INFO,
            AOAHID_LOG_TRACE,
            AOAHID_PROFILE_KEYBOARD,
            AOAHID_PROFILE_MOUSE,
            AOAHID_PROFILE_TOGGLE,
            AOAHID_PROFILE_GAMEPAD,
            AOAHID_PROFILE_TOUCHSCREEN,
            AOAHID_PROFILE_PEN,
            AOAHID_PROFILE_BATTERY,
            AOAHID_PROFILE_RAW,
            AOAHID_PROFILE_TOUCHPAD,
            AOAHID_PROFILE_DIGITIZER,
            AOAHID_ANDROID_PORTABLE_CANDIDATE,
            AOAHID_ANDROID_CONDITIONAL,
            AOAHID_ANDROID_CUSTOM_SYSTEM_ONLY,
            AOAHID_ANDROID_UNSUPPORTED,
            AOAHID_ANDROID_UNKNOWN,
            AOAHID_AXIS_X,
            AOAHID_AXIS_Y,
            AOAHID_AXIS_Z,
            AOAHID_AXIS_RX,
            AOAHID_AXIS_RY,
            AOAHID_AXIS_RZ,
            AOAHID_AXIS_SLIDER,
            AOAHID_AXIS_SIMULATION_ACCELERATOR,
            AOAHID_AXIS_SIMULATION_BRAKE,
            AOAHID_AXIS_SIMULATION_STEERING,
            AOAHID_AXIS_DIAL,
            AOAHID_AXIS_WHEEL,
            AOAHID_AXIS_SIMULATION_RUDDER,
            AOAHID_AXIS_SIMULATION_THROTTLE,
            AOAHID_DPAD_NONE,
            AOAHID_DPAD_HAT,
            AOAHID_DPAD_BUTTONS,
            AOAHID_USAGE_SELECTOR_BITMAP,
            AOAHID_USAGE_ON_OFF_TOGGLE,
            AOAHID_USAGE_ON_OFF_MAINTAINED,
            AOAHID_USAGE_MOMENTARY,
            AOAHID_USAGE_ONE_SHOT,
            AOAHID_USAGE_RETRIGGER,
            AOAHID_USAGE_ON_OFF_PAIR,
            AOAHID_USAGE_LINEAR,
            AOAHID_USAGE_DYNAMIC_VALUE,
            AOAHID_USAGE_NAMED_ARRAY,
        ]
    );
    values
}

fn rust_layout() -> BTreeMap<String, usize> {
    let mut values = BTreeMap::new();
    layout!(
        values,
        aoahid_error_detail,
        "aoahid_error_detail",
        [
            code,
            field,
            reason,
            libusb_status,
            aoa_request,
            hid_id,
            report_id,
            offset,
            length
        ]
    );
    layout!(
        values,
        aoahid_context_options,
        "aoahid_context_options",
        [
            struct_size,
            reserved,
            event_mode,
            log_level,
            log_sink,
            log_user
        ]
    );
    layout!(
        values,
        aoahid_device_info,
        "aoahid_device_info",
        [
            bus_number,
            device_address,
            port_path,
            port_path_length,
            vendor_id,
            product_id,
            protocol_version,
            serial,
            product
        ]
    );
    layout!(
        values,
        aoahid_aoa_strings,
        "aoahid_aoa_strings",
        [manufacturer, model, description, version, uri, serial]
    );
    layout!(
        values,
        aoahid_device_options,
        "aoahid_device_options",
        [
            struct_size,
            reserved,
            control_timeout_ms,
            send_timeout_ms,
            descriptor_fragment_bytes,
            transfer_pool_slots,
            maximum_report_bytes,
            close_drain_timeout_ms,
            aoa_descriptor_wire_policy_bytes,
            linux_descriptor_policy_bytes,
            linux_hid_fields_per_report_policy,
            linux_hid_global_stack_depth_policy,
            linux_hid_usages_policy,
            linux_hid_report_data_bits_policy,
            linux_hid_report_size_bits_policy,
            target_ep0_data_policy_bytes,
            host_control_buffer_policy_bytes,
            interface_claim_policy,
            interface_number,
            validate_reports
        ]
    );
    layout!(
        values,
        aoahid_accessory_options,
        "aoahid_accessory_options",
        [struct_size, reserved, strings, control_timeout_ms]
    );
    layout!(
        values,
        aoahid_channel_options,
        "aoahid_channel_options",
        [
            struct_size,
            reserved,
            interface_class,
            interface_subclass,
            interface_protocol,
            reserved8,
            in_transfers,
            out_transfers,
            transfer_bytes,
            zero_length_termination,
            read_mode
        ]
    );
    layout!(
        values,
        aoahid_node_options,
        "aoahid_node_options",
        [struct_size, reserved, has_reserved_slots, reserved_slots]
    );
    layout!(
        values,
        aoahid_physical_properties,
        "aoahid_physical_properties",
        [enabled, minimum, maximum, unit_exponent, unit]
    );
    layout!(
        values,
        aoahid_integer_field,
        "aoahid_integer_field",
        [logical_minimum, logical_maximum, bit_width, physical]
    );
    layout!(
        values,
        aoahid_report_id_option,
        "aoahid_report_id_option",
        [enabled, value, reserved8]
    );
    layout!(
        values,
        aoahid_keyboard_options,
        "aoahid_keyboard_options",
        [struct_size, reserved, report_id, usage_minimum, usage_maximum]
    );
    layout!(
        values,
        aoahid_mouse_options,
        "aoahid_mouse_options",
        [
            struct_size,
            reserved,
            report_id,
            button_count,
            x,
            y,
            enable_wheel,
            wheel,
            enable_pan,
            pan
        ]
    );
    layout!(
        values,
        aoahid_toggle_options,
        "aoahid_toggle_options",
        [
            struct_size,
            reserved,
            report_id,
            application_page,
            application_usage,
            field_page,
            reserved16,
            allowed_usages,
            allowed_usage_count,
            usage_semantics,
            expected_linux_event_types,
            expected_linux_codes
        ]
    );
    layout!(
        values,
        aoahid_gamepad_axis,
        "aoahid_gamepad_axis",
        [
            role,
            usage_page,
            usage,
            value,
            neutral_value,
            expected_linux_code,
            expected_android_axis
        ]
    );
    layout!(
        values,
        aoahid_gamepad_options,
        "aoahid_gamepad_options",
        [
            struct_size,
            reserved,
            report_id,
            axes,
            axis_count,
            button_count,
            button_usage_minimum,
            dpad_representation,
            hat_logical_minimum,
            hat_logical_maximum,
            hat_bit_width
        ]
    );
    layout!(
        values,
        aoahid_touchscreen_options,
        "aoahid_touchscreen_options",
        [
            struct_size,
            reserved,
            report_id,
            maximum_contacts,
            contacts_per_report,
            contact_identifier,
            x,
            y,
            contact_count,
            enable_pressure,
            pressure,
            enable_width,
            width,
            enable_height,
            height,
            enable_azimuth,
            azimuth,
            enable_scan_time,
            scan_time,
            scan_time_unit_100us,
            enable_contact_count_maximum_feature_declaration,
            enable_multi_packet_frames
        ]
    );
    layout!(
        values,
        aoahid_touchpad_options,
        "aoahid_touchpad_options",
        [
            struct_size,
            reserved,
            report_id,
            maximum_contacts,
            contacts_per_report,
            contact_identifier,
            x,
            y,
            contact_count,
            enable_pressure,
            pressure,
            enable_width,
            width,
            enable_height,
            height,
            enable_azimuth,
            azimuth,
            enable_scan_time,
            scan_time,
            scan_time_unit_100us,
            enable_contact_count_maximum_feature_declaration,
            enable_multi_packet_frames,
            button_count
        ]
    );
    layout!(
        values,
        aoahid_pen_options,
        "aoahid_pen_options",
        [
            struct_size,
            reserved,
            report_id,
            x,
            y,
            enable_pressure,
            pressure,
            enable_tilt,
            tilt_x,
            tilt_y,
            enable_twist_target_specific,
            twist,
            barrel_usages,
            barrel_usage_count,
            enable_eraser,
            enable_hover
        ]
    );
    layout!(
        values,
        aoahid_digitizer_options,
        "aoahid_digitizer_options",
        [
            struct_size,
            reserved,
            report_id,
            x,
            y,
            enable_pressure,
            pressure,
            enable_tilt,
            tilt_x,
            tilt_y,
            enable_twist_target_specific,
            twist,
            barrel_usages,
            barrel_usage_count,
            enable_eraser,
            enable_hover
        ]
    );
    layout!(
        values,
        aoahid_battery_options,
        "aoahid_battery_options",
        [
            struct_size,
            reserved,
            report_id,
            strength,
            enable_unknown_null_state
        ]
    );
    layout!(
        values,
        aoahid_raw_report,
        "aoahid_raw_report",
        [report_id, has_report_id, reserved16, wire_length]
    );
    layout!(
        values,
        aoahid_raw_options,
        "aoahid_raw_options",
        [
            struct_size,
            reserved,
            descriptor,
            descriptor_length,
            reports,
            report_count,
            acknowledges_no_android_support,
            requires_output,
            requires_feature_response
        ]
    );
    layout!(
        values,
        aoahid_report_capability,
        "aoahid_report_capability",
        [report_id, has_report_id, reserved16, wire_length]
    );
    layout!(
        values,
        aoahid_capability_manifest,
        "aoahid_capability_manifest",
        [
            struct_size,
            reserved,
            input_supported,
            output_supported,
            feature_transport_supported,
            android_status,
            profile_kind,
            reports,
            report_count,
            descriptor_bytes
        ]
    );
    layout!(
        values,
        aoahid_touch_contact,
        "aoahid_touch_contact",
        [contact_id, x, y, pressure, width, height, azimuth]
    );
    layout!(
        values,
        aoahid_touch_extra,
        "aoahid_touch_extra",
        [pressure, width, height, azimuth]
    );
    layout!(
        values,
        aoahid_pen_sample,
        "aoahid_pen_sample",
        [
            in_range,
            tip,
            eraser,
            barrel_buttons,
            x,
            y,
            pressure,
            tilt_x,
            tilt_y,
            twist
        ]
    );
    values
}

fn c_oracle(executable: &str) -> BTreeMap<String, usize> {
    let output = Command::new(executable)
        .output()
        .expect("run compiled C ABI layout oracle");
    assert!(output.status.success());
    String::from_utf8(output.stdout)
        .expect("oracle output is UTF-8")
        .lines()
        .map(|line| {
            let (name, value) = line.split_once('=').expect("name=value oracle line");
            (
                name.to_owned(),
                value.parse().expect("numeric oracle value"),
            )
        })
        .collect()
}

#[test]
fn every_layout_matches_compiled_c_header() {
    let executable = std::env::var("AOAHID_ABI_ORACLE")
        .expect("AOAHID_ABI_ORACLE must name the compiled C oracle");
    let layout = c_oracle(&executable)
        .into_iter()
        .filter(|(name, _)| name.starts_with("aoahid_"))
        .collect();
    assert_eq!(rust_layout(), layout);
}

#[test]
fn every_constant_matches_compiled_c_header() {
    let executable = std::env::var("AOAHID_ABI_ORACLE")
        .expect("AOAHID_ABI_ORACLE must name the compiled C oracle");
    let constants = c_oracle(&executable)
        .into_iter()
        .filter(|(name, _)| name.starts_with("AOAHID_"))
        .collect();
    assert_eq!(rust_constants(), constants);
}
