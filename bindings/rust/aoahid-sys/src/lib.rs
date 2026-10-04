// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
//! Mirrors the stable C ABI literally; policy and ownership wrappers live in
//! the separate safe crate.

#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};

macro_rules! opaque {
    ($($name:ident),+ $(,)?) => {$(
        #[repr(C)]
        pub struct $name { _private: [u8; 0] }
    )+};
}
opaque!(
    aoahid_context,
    aoahid_discovery,
    aoahid_device,
    aoahid_spec,
    aoahid_node,
    aoahid_channel
);

pub type aoahid_result = i32;
pub type aoahid_event_mode = i32;
pub type aoahid_channel_read_mode = i32;
pub type aoahid_interface_claim_policy = i32;
pub type aoahid_log_level = i32;
pub type aoahid_profile_kind = i32;
pub type aoahid_android_status = i32;
pub type aoahid_pen_mode = i32;
pub type aoahid_axis_role = i32;
pub type aoahid_dpad_representation = i32;
pub type aoahid_usage_semantic = i32;

pub const AOAHID_VERSION_MAJOR: u32 = 4;
pub const AOAHID_VERSION_MINOR: u32 = 0;
pub const AOAHID_VERSION_PATCH: u32 = 5;
pub const AOAHID_OK: i32 = 0;
pub const AOAHID_ERR_PARAM: i32 = 1;
pub const AOAHID_ERR_UNSET_FIELD: i32 = 2;
pub const AOAHID_ERR_UNSUPPORTED: i32 = 3;
pub const AOAHID_ERR_NOT_AOA: i32 = 4;
pub const AOAHID_ERR_ACCESS: i32 = 6;
pub const AOAHID_ERR_BUSY: i32 = 7;
pub const AOAHID_ERR_NO_DEVICE: i32 = 8;
pub const AOAHID_ERR_STALL: i32 = 9;
pub const AOAHID_ERR_TIMEOUT: i32 = 10;
pub const AOAHID_ERR_SHORT_TRANSFER: i32 = 11;
pub const AOAHID_ERR_DESCRIPTOR_REJECTED: i32 = 12;
pub const AOAHID_ERR_IO: i32 = 13;
pub const AOAHID_ERR_OVERFLOW: i32 = 14;
pub const AOAHID_CLOSE_PENDING: i32 = 15;
pub const AOAHID_ERR_INTERNAL: i32 = 16;

pub const AOAHID_EVENT_CALLER_POLL: i32 = 1;
pub const AOAHID_EVENT_INTERNAL_THREAD: i32 = 2;
pub const AOAHID_CHANNEL_READ_STREAM: i32 = 0;
pub const AOAHID_CHANNEL_READ_REQUEST: i32 = 1;
pub const AOAHID_INTERFACE_CLAIM_NONE: i32 = 1;
pub const AOAHID_INTERFACE_CLAIM_EXPLICIT: i32 = 2;
pub const AOAHID_LOG_DISABLED: i32 = 1;
pub const AOAHID_LOG_ERROR: i32 = 2;
pub const AOAHID_LOG_INFO: i32 = 3;
pub const AOAHID_LOG_TRACE: i32 = 4;

pub const AOAHID_PROFILE_KEYBOARD: i32 = 1;
pub const AOAHID_PROFILE_MOUSE: i32 = 2;
pub const AOAHID_PROFILE_TOGGLE: i32 = 3;
pub const AOAHID_PROFILE_GAMEPAD: i32 = 4;
pub const AOAHID_PROFILE_TOUCHSCREEN: i32 = 5;
pub const AOAHID_PROFILE_PEN: i32 = 6;
pub const AOAHID_PROFILE_BATTERY: i32 = 7;
pub const AOAHID_PROFILE_RAW: i32 = 8;
pub const AOAHID_PROFILE_TOUCHPAD: i32 = 9;
pub const AOAHID_ANDROID_PORTABLE_CANDIDATE: i32 = 1;
pub const AOAHID_ANDROID_CONDITIONAL: i32 = 2;
pub const AOAHID_ANDROID_CUSTOM_SYSTEM_ONLY: i32 = 3;
pub const AOAHID_ANDROID_UNSUPPORTED: i32 = 4;
pub const AOAHID_ANDROID_UNKNOWN: i32 = 5;
pub const AOAHID_PEN_DIRECT_SCREEN: i32 = 1;
pub const AOAHID_PEN_INDIRECT_TABLET: i32 = 2;

pub const AOAHID_AXIS_X: i32 = 1;
pub const AOAHID_AXIS_Y: i32 = 2;
pub const AOAHID_AXIS_Z: i32 = 3;
pub const AOAHID_AXIS_RX: i32 = 4;
pub const AOAHID_AXIS_RY: i32 = 5;
pub const AOAHID_AXIS_RZ: i32 = 6;
pub const AOAHID_AXIS_SLIDER: i32 = 7;
pub const AOAHID_AXIS_SIMULATION_ACCELERATOR: i32 = 8;
pub const AOAHID_AXIS_SIMULATION_BRAKE: i32 = 9;
pub const AOAHID_AXIS_SIMULATION_STEERING: i32 = 10;
pub const AOAHID_AXIS_DIAL: i32 = 11;
pub const AOAHID_AXIS_WHEEL: i32 = 12;
pub const AOAHID_AXIS_SIMULATION_RUDDER: i32 = 13;
pub const AOAHID_AXIS_SIMULATION_THROTTLE: i32 = 14;
pub const AOAHID_DPAD_NONE: i32 = 1;
pub const AOAHID_DPAD_HAT: i32 = 2;
pub const AOAHID_DPAD_BUTTONS: i32 = 3;

pub const AOAHID_USAGE_SELECTOR_BITMAP: i32 = 1;
pub const AOAHID_USAGE_ON_OFF_TOGGLE: i32 = 2;
pub const AOAHID_USAGE_ON_OFF_MAINTAINED: i32 = 3;
pub const AOAHID_USAGE_MOMENTARY: i32 = 4;
pub const AOAHID_USAGE_ONE_SHOT: i32 = 5;
pub const AOAHID_USAGE_RETRIGGER: i32 = 6;
pub const AOAHID_USAGE_ON_OFF_PAIR: i32 = 7;
pub const AOAHID_USAGE_LINEAR: i32 = 8;
pub const AOAHID_USAGE_DYNAMIC_VALUE: i32 = 9;
pub const AOAHID_USAGE_NAMED_ARRAY: i32 = 10;

pub type aoahid_log_sink = Option<
    unsafe extern "C" fn(user: *mut c_void, level: aoahid_log_level, message: *const c_char),
>;

macro_rules! c_struct {
    ($name:ident { $($field:ident: $kind:ty),+ $(,)? }) => {
        #[repr(C)]
        pub struct $name { $(pub $field: $kind),+ }
    };
}

c_struct!(aoahid_error_detail {
    code: i32, field: *const c_char, reason: *const c_char, libusb_status: i32,
    aoa_request: i32, hid_id: u16, report_id: u16, offset: u32, length: u32,
});
c_struct!(aoahid_context_options {
    struct_size: u32, reserved: u32, event_mode: aoahid_event_mode,
    log_level: aoahid_log_level,
    log_sink: aoahid_log_sink, log_user: *mut c_void,
});
c_struct!(aoahid_device_info {
    bus_number: u8, device_address: u8, port_path: *const u8, port_path_length: usize,
    vendor_id: u16, product_id: u16, protocol_version: u16, serial: *const c_char,
    product: *const c_char,
});
c_struct!(aoahid_aoa_strings {
    manufacturer: *const c_char, model: *const c_char, description: *const c_char,
    version: *const c_char, uri: *const c_char, serial: *const c_char,
});
c_struct!(aoahid_device_options {
    struct_size: u32,
    reserved: u32,
    control_timeout_ms: u32,
    send_timeout_ms: u32,
    descriptor_fragment_bytes: u32,
    transfer_pool_slots: u32,
    maximum_report_bytes: u32,
    close_drain_timeout_ms: u32,
    aoa_descriptor_wire_policy_bytes: u32,
    linux_descriptor_policy_bytes: u32,
    linux_hid_fields_per_report_policy: u32,
    linux_hid_global_stack_depth_policy: u32,
    linux_hid_usages_policy: u64,
    linux_hid_report_data_bits_policy: u64,
    linux_hid_report_size_bits_policy: u32,
    target_ep0_data_policy_bytes: u32,
    host_control_buffer_policy_bytes: u32,
    interface_claim_policy: aoahid_interface_claim_policy,
    interface_number: i32,
    validate_reports: u32,
});
c_struct!(aoahid_accessory_options {
    struct_size: u32,
    reserved: u32,
    strings: aoahid_aoa_strings,
    control_timeout_ms: u32,
});
c_struct!(aoahid_channel_options {
    struct_size: u32,
    reserved: u32,
    interface_class: u8,
    interface_subclass: u8,
    interface_protocol: u8,
    reserved8: u8,
    in_transfers: u32,
    out_transfers: u32,
    transfer_bytes: u32,
    zero_length_termination: u32,
    read_mode: aoahid_channel_read_mode,
});
c_struct!(aoahid_node_options {
    struct_size: u32,
    reserved: u32,
    has_reserved_slots: u32,
    reserved_slots: u32,
});
c_struct!(aoahid_physical_properties {
    enabled: u32,
    minimum: i32,
    maximum: i32,
    unit_exponent: i32,
    unit: u32,
});
c_struct!(aoahid_integer_field {
    logical_minimum: i32,
    logical_maximum: i32,
    bit_width: u32,
    physical: aoahid_physical_properties,
});
c_struct!(aoahid_report_id_option {
    enabled: u32,
    value: u8,
    reserved8: [u8; 3],
});
c_struct!(aoahid_keyboard_options {
    struct_size: u32,
    reserved: u32,
    report_id: aoahid_report_id_option,
    usage_minimum: u16,
    usage_maximum: u16,
});
c_struct!(aoahid_mouse_options {
    struct_size: u32,
    reserved: u32,
    report_id: aoahid_report_id_option,
    button_count: u32,
    x: aoahid_integer_field,
    y: aoahid_integer_field,
    enable_wheel: u32,
    wheel: aoahid_integer_field,
    enable_pan: u32,
    pan: aoahid_integer_field,
});
c_struct!(aoahid_toggle_options {
    struct_size: u32, reserved: u32, report_id: aoahid_report_id_option,
    application_page: u16, application_usage: u16, field_page: u16, reserved16: u16,
    allowed_usages: *const u16, allowed_usage_count: usize,
    usage_semantics: *const aoahid_usage_semantic,
    expected_linux_event_types: *const *const c_char,
    expected_linux_codes: *const *const c_char,
});
c_struct!(aoahid_gamepad_axis {
    role: aoahid_axis_role, usage_page: u16, usage: u16, value: aoahid_integer_field,
    neutral_value: i32, expected_linux_code: *const c_char,
    expected_android_axis: *const c_char,
});
c_struct!(aoahid_gamepad_options {
    struct_size: u32, reserved: u32, report_id: aoahid_report_id_option,
    axes: *const aoahid_gamepad_axis,
    axis_count: usize, button_count: u32, button_usage_minimum: u16,
    dpad_representation: aoahid_dpad_representation,
    hat_logical_minimum: i32, hat_logical_maximum: i32, hat_bit_width: u32,
});
c_struct!(aoahid_touchscreen_options {
    struct_size: u32,
    reserved: u32,
    report_id: aoahid_report_id_option,
    maximum_contacts: u32,
    contacts_per_report: u32,
    contact_identifier: aoahid_integer_field,
    x: aoahid_integer_field,
    y: aoahid_integer_field,
    contact_count: aoahid_integer_field,
    enable_pressure: u32,
    pressure: aoahid_integer_field,
    enable_width: u32,
    width: aoahid_integer_field,
    enable_height: u32,
    height: aoahid_integer_field,
    enable_azimuth: u32,
    azimuth: aoahid_integer_field,
    enable_scan_time: u32,
    scan_time: aoahid_integer_field,
    scan_time_unit_100us: u32,
    enable_contact_count_maximum_feature_declaration: u32,
    enable_multi_packet_frames: u32,
});
c_struct!(aoahid_touchpad_options {
    struct_size: u32,
    reserved: u32,
    report_id: aoahid_report_id_option,
    maximum_contacts: u32,
    contacts_per_report: u32,
    contact_identifier: aoahid_integer_field,
    x: aoahid_integer_field,
    y: aoahid_integer_field,
    contact_count: aoahid_integer_field,
    enable_pressure: u32,
    pressure: aoahid_integer_field,
    enable_width: u32,
    width: aoahid_integer_field,
    enable_height: u32,
    height: aoahid_integer_field,
    enable_azimuth: u32,
    azimuth: aoahid_integer_field,
    enable_scan_time: u32,
    scan_time: aoahid_integer_field,
    scan_time_unit_100us: u32,
    enable_contact_count_maximum_feature_declaration: u32,
    enable_multi_packet_frames: u32,
    button_count: u32,
});
c_struct!(aoahid_pen_options {
    struct_size: u32, reserved: u32, report_id: aoahid_report_id_option,
    mode: aoahid_pen_mode,
    x: aoahid_integer_field, y: aoahid_integer_field, enable_pressure: u32,
    pressure: aoahid_integer_field, enable_tilt: u32, tilt_x: aoahid_integer_field,
    tilt_y: aoahid_integer_field, enable_twist_target_specific: u32,
    twist: aoahid_integer_field, barrel_usages: *const u16,
    barrel_usage_count: usize, enable_eraser: u32, enable_hover: u32,
});
c_struct!(aoahid_battery_options {
    struct_size: u32,
    reserved: u32,
    report_id: aoahid_report_id_option,
    strength: aoahid_integer_field,
    enable_unknown_null_state: u32,
});
c_struct!(aoahid_raw_report {
    report_id: u8,
    has_report_id: u8,
    reserved16: u16,
    wire_length: u32,
});
c_struct!(aoahid_raw_options {
    struct_size: u32, reserved: u32, descriptor: *const u8, descriptor_length: usize,
    reports: *const aoahid_raw_report, report_count: usize,
    acknowledges_no_android_support: u32, requires_output: u32,
    requires_feature_response: u32,
});
c_struct!(aoahid_report_capability {
    report_id: u8,
    has_report_id: u8,
    reserved16: u16,
    wire_length: u32,
});
c_struct!(aoahid_capability_manifest {
    struct_size: u32, reserved: u32, input_supported: u32, output_supported: u32,
    feature_transport_supported: u32, android_status: aoahid_android_status,
    profile_kind: aoahid_profile_kind,
    reports: *const aoahid_report_capability, report_count: usize,
    descriptor_bytes: usize,
});
c_struct!(aoahid_touch_contact {
    contact_id: u32,
    x: i32,
    y: i32,
    pressure: i32,
    width: i32,
    height: i32,
    azimuth: i32,
});
c_struct!(aoahid_touch_extra {
    pressure: i32,
    width: i32,
    height: i32,
    azimuth: i32,
});
c_struct!(aoahid_pen_sample {
    in_range: u32,
    tip: u32,
    eraser: u32,
    barrel_buttons: u32,
    x: i32,
    y: i32,
    pressure: i32,
    tilt_x: i32,
    tilt_y: i32,
    twist: i32,
});

extern "C" {
    pub fn aoahid_last_error() -> *const aoahid_error_detail;
    pub fn aoahid_result_name(result: aoahid_result) -> *const c_char;
    pub fn aoahid_version() -> u32;
    pub fn aoahid_context_create(
        options: *const aoahid_context_options,
        out_context: *mut *mut aoahid_context,
    ) -> aoahid_result;
    pub fn aoahid_context_poll(context: *mut aoahid_context, timeout_ms: u32) -> aoahid_result;
    pub fn aoahid_context_destroy(context: *mut aoahid_context) -> aoahid_result;
    pub fn aoahid_context_destroy_blocking(
        context: *mut aoahid_context,
        timeout_ms: u32,
    ) -> aoahid_result;
    pub fn aoahid_discover(
        context: *mut aoahid_context,
        control_timeout_ms: u32,
        out_discovery: *mut *mut aoahid_discovery,
    ) -> aoahid_result;
    pub fn aoahid_discovery_count(discovery: *const aoahid_discovery) -> usize;
    pub fn aoahid_discovery_get(
        discovery: *const aoahid_discovery,
        index: usize,
    ) -> *const aoahid_device_info;
    pub fn aoahid_discovery_destroy(discovery: *mut aoahid_discovery);
    pub fn aoahid_accessory_start(
        context: *mut aoahid_context,
        selected: *const aoahid_device_info,
        options: *const aoahid_accessory_options,
    ) -> aoahid_result;
    pub fn aoahid_device_open(
        context: *mut aoahid_context,
        selected: *const aoahid_device_info,
        options: *const aoahid_device_options,
        out_device: *mut *mut aoahid_device,
    ) -> aoahid_result;
    pub fn aoahid_device_close(device: *mut aoahid_device) -> aoahid_result;
    pub fn aoahid_device_latched_error(device: *mut aoahid_device) -> aoahid_result;
    pub fn aoahid_channel_open(
        device: *mut aoahid_device,
        options: *const aoahid_channel_options,
        out_channel: *mut *mut aoahid_channel,
    ) -> aoahid_result;
    pub fn aoahid_channel_close(channel: *mut aoahid_channel) -> aoahid_result;
    pub fn aoahid_channel_write(
        channel: *mut aoahid_channel,
        data: *const u8,
        length: usize,
        out_written: *mut usize,
        timeout_ms: u32,
    ) -> aoahid_result;
    pub fn aoahid_channel_read(
        channel: *mut aoahid_channel,
        buffer: *mut u8,
        capacity: usize,
        out_received: *mut usize,
        timeout_ms: u32,
    ) -> aoahid_result;

    pub fn aoahid_spec_create_keyboard(
        options: *const aoahid_keyboard_options,
        out_spec: *mut *mut aoahid_spec,
    ) -> aoahid_result;
    pub fn aoahid_spec_create_mouse(
        options: *const aoahid_mouse_options,
        out_spec: *mut *mut aoahid_spec,
    ) -> aoahid_result;
    pub fn aoahid_spec_create_toggle(
        options: *const aoahid_toggle_options,
        out_spec: *mut *mut aoahid_spec,
    ) -> aoahid_result;
    pub fn aoahid_spec_create_gamepad(
        options: *const aoahid_gamepad_options,
        out_spec: *mut *mut aoahid_spec,
    ) -> aoahid_result;
    pub fn aoahid_spec_create_touchscreen(
        options: *const aoahid_touchscreen_options,
        out_spec: *mut *mut aoahid_spec,
    ) -> aoahid_result;
    pub fn aoahid_spec_create_touchpad(
        options: *const aoahid_touchpad_options,
        out_spec: *mut *mut aoahid_spec,
    ) -> aoahid_result;
    pub fn aoahid_spec_create_pen(
        options: *const aoahid_pen_options,
        out_spec: *mut *mut aoahid_spec,
    ) -> aoahid_result;
    pub fn aoahid_spec_create_battery(
        options: *const aoahid_battery_options,
        out_spec: *mut *mut aoahid_spec,
    ) -> aoahid_result;
    pub fn aoahid_spec_create_raw(
        options: *const aoahid_raw_options,
        out_spec: *mut *mut aoahid_spec,
    ) -> aoahid_result;
    pub fn aoahid_spec_retain(spec: *mut aoahid_spec);
    pub fn aoahid_spec_release(spec: *mut aoahid_spec);
    pub fn aoahid_spec_descriptor(
        spec: *const aoahid_spec,
        bytes: *mut *const u8,
        length: *mut usize,
    ) -> aoahid_result;
    pub fn aoahid_spec_manifest(
        spec: *const aoahid_spec,
        manifest: *mut aoahid_capability_manifest,
    ) -> aoahid_result;

    pub fn aoahid_node_open(
        device: *mut aoahid_device,
        spec: *mut aoahid_spec,
        options: *const aoahid_node_options,
        out_node: *mut *mut aoahid_node,
    ) -> aoahid_result;
    pub fn aoahid_node_close(node: *mut aoahid_node) -> aoahid_result;
    pub fn aoahid_node_hid_id(node: *const aoahid_node) -> u16;
    pub fn aoahid_node_manifest(
        node: *const aoahid_node,
        manifest: *mut aoahid_capability_manifest,
    ) -> aoahid_result;
    pub fn aoahid_node_submit(node: *mut aoahid_node) -> aoahid_result;
    pub fn aoahid_node_submit_blocking(node: *mut aoahid_node, deadline_ms: u32) -> aoahid_result;
    pub fn aoahid_kbd(node: *mut aoahid_node, usage: u16, down: u32) -> aoahid_result;
    pub fn aoahid_mouse_move(node: *mut aoahid_node, dx: i32, dy: i32) -> aoahid_result;
    pub fn aoahid_mouse_scroll(node: *mut aoahid_node, wheel: i32, pan: i32) -> aoahid_result;
    pub fn aoahid_mouse_button(node: *mut aoahid_node, button: u32, pressed: u32) -> aoahid_result;
    pub fn aoahid_toggle(node: *mut aoahid_node, usage: u16, down: u32) -> aoahid_result;
    pub fn aoahid_gamepad_button(
        node: *mut aoahid_node,
        button: u32,
        pressed: u32,
    ) -> aoahid_result;
    pub fn aoahid_gamepad_set_axis(
        node: *mut aoahid_node,
        axis_index: usize,
        value: i32,
    ) -> aoahid_result;
    pub fn aoahid_dpad(
        node: *mut aoahid_node,
        up: u32,
        down: u32,
        right: u32,
        left: u32,
    ) -> aoahid_result;
    pub fn aoahid_touch(
        node: *mut aoahid_node,
        contact_id: u32,
        down: u32,
        x: i32,
        y: i32,
        extra: *const aoahid_touch_extra,
    ) -> aoahid_result;
    pub fn aoahid_touchpad_button(node: *mut aoahid_node, button: u32, pressed: u32)
        -> aoahid_result;
    pub fn aoahid_pen_update(
        node: *mut aoahid_node,
        sample: *const aoahid_pen_sample,
    ) -> aoahid_result;
    pub fn aoahid_pen_depart(node: *mut aoahid_node) -> aoahid_result;
    pub fn aoahid_battery_update(
        node: *mut aoahid_node,
        has_value: u32,
        strength: i32,
    ) -> aoahid_result;
    pub fn aoahid_raw_submit(
        node: *mut aoahid_node,
        report: *const u8,
        length: usize,
    ) -> aoahid_result;
}
