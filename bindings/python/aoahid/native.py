# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libaoahid contributors
"""Complete, literal ctypes declarations for the stable ``aoahid.h`` C ABI.

The binding chooses no option overrides. Callers fill ``struct_size`` and every
product/target field; the native C API applies its documented bounded fallback
to a zero-valued host-transport tuning field.
"""

from __future__ import annotations

import ctypes as c
import os
from typing import Union


# All public enum domains in aoahid.h are exactly int32_t.
AOAHID_VERSION_MAJOR = 4
AOAHID_VERSION_MINOR = 2
AOAHID_VERSION_PATCH = 0
AOAHID_OK = 0
AOAHID_ERR_PARAM = 1
AOAHID_ERR_UNSET_FIELD = 2
AOAHID_ERR_UNSUPPORTED = 3
AOAHID_ERR_NOT_AOA = 4
AOAHID_ERR_ACCESS = 6
AOAHID_ERR_BUSY = 7
AOAHID_ERR_NO_DEVICE = 8
AOAHID_ERR_STALL = 9
AOAHID_ERR_TIMEOUT = 10
AOAHID_ERR_SHORT_TRANSFER = 11
AOAHID_ERR_DESCRIPTOR_REJECTED = 12
AOAHID_ERR_IO = 13
AOAHID_ERR_OVERFLOW = 14
AOAHID_CLOSE_PENDING = 15
AOAHID_ERR_INTERNAL = 16

AOAHID_EVENT_CALLER_POLL = 1
AOAHID_EVENT_INTERNAL_THREAD = 2
AOAHID_CHANNEL_READ_STREAM = 0
AOAHID_CHANNEL_READ_REQUEST = 1
AOAHID_INTERFACE_CLAIM_NONE = 1
AOAHID_INTERFACE_CLAIM_EXPLICIT = 2
AOAHID_LOG_DISABLED = 1
AOAHID_LOG_ERROR = 2
AOAHID_LOG_INFO = 3
AOAHID_LOG_TRACE = 4

AOAHID_PROFILE_KEYBOARD = 1
AOAHID_PROFILE_MOUSE = 2
AOAHID_PROFILE_TOGGLE = 3
AOAHID_PROFILE_GAMEPAD = 4
AOAHID_PROFILE_TOUCHSCREEN = 5
AOAHID_PROFILE_PEN = 6
AOAHID_PROFILE_BATTERY = 7
AOAHID_PROFILE_RAW = 8
AOAHID_PROFILE_TOUCHPAD = 9

AOAHID_ANDROID_PORTABLE_CANDIDATE = 1
AOAHID_ANDROID_CONDITIONAL = 2
AOAHID_ANDROID_CUSTOM_SYSTEM_ONLY = 3
AOAHID_ANDROID_UNSUPPORTED = 4
AOAHID_ANDROID_UNKNOWN = 5
AOAHID_PEN_DIRECT_SCREEN = 1
AOAHID_PEN_INDIRECT_TABLET = 2

AOAHID_AXIS_X = 1
AOAHID_AXIS_Y = 2
AOAHID_AXIS_Z = 3
AOAHID_AXIS_RX = 4
AOAHID_AXIS_RY = 5
AOAHID_AXIS_RZ = 6
AOAHID_AXIS_SLIDER = 7
AOAHID_AXIS_SIMULATION_ACCELERATOR = 8
AOAHID_AXIS_SIMULATION_BRAKE = 9
AOAHID_AXIS_SIMULATION_STEERING = 10
AOAHID_AXIS_DIAL = 11
AOAHID_AXIS_WHEEL = 12
AOAHID_AXIS_SIMULATION_RUDDER = 13
AOAHID_AXIS_SIMULATION_THROTTLE = 14
AOAHID_DPAD_NONE = 1
AOAHID_DPAD_HAT = 2
AOAHID_DPAD_BUTTONS = 3

AOAHID_USAGE_SELECTOR_BITMAP = 1
AOAHID_USAGE_ON_OFF_TOGGLE = 2
AOAHID_USAGE_ON_OFF_MAINTAINED = 3
AOAHID_USAGE_MOMENTARY = 4
AOAHID_USAGE_ONE_SHOT = 5
AOAHID_USAGE_RETRIGGER = 6
AOAHID_USAGE_ON_OFF_PAIR = 7
AOAHID_USAGE_LINEAR = 8
AOAHID_USAGE_DYNAMIC_VALUE = 9
AOAHID_USAGE_NAMED_ARRAY = 10


class Context(c.Structure):
    pass


class Discovery(c.Structure):
    pass


class Device(c.Structure):
    pass


class Spec(c.Structure):
    pass


class Node(c.Structure):
    pass


class Channel(c.Structure):
    pass


ContextP = c.POINTER(Context)
DiscoveryP = c.POINTER(Discovery)
DeviceP = c.POINTER(Device)
SpecP = c.POINTER(Spec)
NodeP = c.POINTER(Node)
ChannelP = c.POINTER(Channel)
LogSink = c.CFUNCTYPE(None, c.c_void_p, c.c_int32, c.c_char_p)


class ErrorDetail(c.Structure):
    _fields_ = [
        ("code", c.c_int32),
        ("field", c.c_char_p),
        ("reason", c.c_char_p),
        ("libusb_status", c.c_int32),
        ("aoa_request", c.c_int32),
        ("hid_id", c.c_uint16),
        ("report_id", c.c_uint16),
        ("offset", c.c_uint32),
        ("length", c.c_uint32),
    ]


class ContextOptions(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("event_mode", c.c_int32),
        ("log_level", c.c_int32),
        ("log_sink", LogSink),
        ("log_user", c.c_void_p),
    ]


class DeviceInfo(c.Structure):
    _fields_ = [
        ("bus_number", c.c_uint8),
        ("device_address", c.c_uint8),
        ("port_path", c.POINTER(c.c_uint8)),
        ("port_path_length", c.c_size_t),
        ("vendor_id", c.c_uint16),
        ("product_id", c.c_uint16),
        ("protocol_version", c.c_uint16),
        ("serial", c.c_char_p),
        ("product", c.c_char_p),
    ]


class AoaStrings(c.Structure):
    # AOA request-52 strings. aoahid_accessory_start requires manufacturer and
    # model.
    _fields_ = [
        ("manufacturer", c.c_char_p),
        ("model", c.c_char_p),
        ("description", c.c_char_p),
        ("version", c.c_char_p),
        ("uri", c.c_char_p),
        ("serial", c.c_char_p),
    ]


class DeviceOptions(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("control_timeout_ms", c.c_uint32),
        ("send_timeout_ms", c.c_uint32),
        ("descriptor_fragment_bytes", c.c_uint32),
        ("transfer_pool_slots", c.c_uint32),
        ("maximum_report_bytes", c.c_uint32),
        ("close_drain_timeout_ms", c.c_uint32),
        ("aoa_descriptor_wire_policy_bytes", c.c_uint32),
        ("linux_descriptor_policy_bytes", c.c_uint32),
        ("linux_hid_fields_per_report_policy", c.c_uint32),
        ("linux_hid_global_stack_depth_policy", c.c_uint32),
        ("linux_hid_usages_policy", c.c_uint64),
        ("linux_hid_report_data_bits_policy", c.c_uint64),
        ("linux_hid_report_size_bits_policy", c.c_uint32),
        ("target_ep0_data_policy_bytes", c.c_uint32),
        ("host_control_buffer_policy_bytes", c.c_uint32),
        ("interface_claim_policy", c.c_int32),
        ("interface_number", c.c_int32),
        ("validate_reports", c.c_uint32),
    ]


class AccessoryOptions(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("strings", AoaStrings),
        ("control_timeout_ms", c.c_uint32),
    ]


class ChannelOptions(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("interface_class", c.c_uint8),
        ("interface_subclass", c.c_uint8),
        ("interface_protocol", c.c_uint8),
        ("reserved8", c.c_uint8),
        ("in_transfers", c.c_uint32),
        ("out_transfers", c.c_uint32),
        ("transfer_bytes", c.c_uint32),
        ("zero_length_termination", c.c_uint32),
        ("read_mode", c.c_int32),
    ]


class NodeOptions(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("has_reserved_slots", c.c_uint32),
        ("reserved_slots", c.c_uint32),
    ]


class PhysicalProperties(c.Structure):
    _fields_ = [
        ("enabled", c.c_uint32),
        ("minimum", c.c_int32),
        ("maximum", c.c_int32),
        ("unit_exponent", c.c_int32),
        ("unit", c.c_uint32),
    ]


class IntegerField(c.Structure):
    _fields_ = [
        ("logical_minimum", c.c_int32),
        ("logical_maximum", c.c_int32),
        ("bit_width", c.c_uint32),
        ("physical", PhysicalProperties),
    ]


class ReportId(c.Structure):
    _fields_ = [
        ("enabled", c.c_uint32),
        ("value", c.c_uint8),
        ("reserved8", c.c_uint8 * 3),
    ]


class KeyboardOptions(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("report_id", ReportId),
        ("usage_minimum", c.c_uint16),
        ("usage_maximum", c.c_uint16),
    ]


class MouseOptions(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("report_id", ReportId),
        ("button_count", c.c_uint32),
        ("x", IntegerField),
        ("y", IntegerField),
        ("enable_wheel", c.c_uint32),
        ("wheel", IntegerField),
        ("enable_pan", c.c_uint32),
        ("pan", IntegerField),
    ]


class ToggleOptions(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("report_id", ReportId),
        ("application_page", c.c_uint16),
        ("application_usage", c.c_uint16),
        ("field_page", c.c_uint16),
        ("reserved16", c.c_uint16),
        ("allowed_usages", c.POINTER(c.c_uint16)),
        ("allowed_usage_count", c.c_size_t),
        ("usage_semantics", c.POINTER(c.c_int32)),
        ("expected_linux_event_types", c.POINTER(c.c_char_p)),
        ("expected_linux_codes", c.POINTER(c.c_char_p)),
    ]


class GamepadAxis(c.Structure):
    _fields_ = [
        ("role", c.c_int32),
        ("usage_page", c.c_uint16),
        ("usage", c.c_uint16),
        ("value", IntegerField),
        ("neutral_value", c.c_int32),
        ("expected_linux_code", c.c_char_p),
        ("expected_android_axis", c.c_char_p),
    ]


class GamepadOptions(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("report_id", ReportId),
        ("axes", c.POINTER(GamepadAxis)),
        ("axis_count", c.c_size_t),
        ("button_count", c.c_uint32),
        ("button_usage_minimum", c.c_uint16),
        ("dpad_representation", c.c_int32),
        ("hat_logical_minimum", c.c_int32),
        ("hat_logical_maximum", c.c_int32),
        ("hat_bit_width", c.c_uint32),
    ]


class TouchscreenOptions(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("report_id", ReportId),
        ("maximum_contacts", c.c_uint32),
        ("contacts_per_report", c.c_uint32),
        ("contact_identifier", IntegerField),
        ("x", IntegerField),
        ("y", IntegerField),
        ("contact_count", IntegerField),
        ("enable_pressure", c.c_uint32),
        ("pressure", IntegerField),
        ("enable_width", c.c_uint32),
        ("width", IntegerField),
        ("enable_height", c.c_uint32),
        ("height", IntegerField),
        ("enable_azimuth", c.c_uint32),
        ("azimuth", IntegerField),
        ("enable_scan_time", c.c_uint32),
        ("scan_time", IntegerField),
        ("scan_time_unit_100us", c.c_uint32),
        ("enable_contact_count_maximum_feature_declaration", c.c_uint32),
        ("enable_multi_packet_frames", c.c_uint32),
    ]


class TouchpadOptions(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("report_id", ReportId),
        ("maximum_contacts", c.c_uint32),
        ("contacts_per_report", c.c_uint32),
        ("contact_identifier", IntegerField),
        ("x", IntegerField),
        ("y", IntegerField),
        ("contact_count", IntegerField),
        ("enable_pressure", c.c_uint32),
        ("pressure", IntegerField),
        ("enable_width", c.c_uint32),
        ("width", IntegerField),
        ("enable_height", c.c_uint32),
        ("height", IntegerField),
        ("enable_azimuth", c.c_uint32),
        ("azimuth", IntegerField),
        ("enable_scan_time", c.c_uint32),
        ("scan_time", IntegerField),
        ("scan_time_unit_100us", c.c_uint32),
        ("enable_contact_count_maximum_feature_declaration", c.c_uint32),
        ("enable_multi_packet_frames", c.c_uint32),
        ("button_count", c.c_uint32),
    ]


class PenOptions(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("report_id", ReportId),
        ("mode", c.c_int32),
        ("x", IntegerField),
        ("y", IntegerField),
        ("enable_pressure", c.c_uint32),
        ("pressure", IntegerField),
        ("enable_tilt", c.c_uint32),
        ("tilt_x", IntegerField),
        ("tilt_y", IntegerField),
        ("enable_twist_target_specific", c.c_uint32),
        ("twist", IntegerField),
        ("barrel_usages", c.POINTER(c.c_uint16)),
        ("barrel_usage_count", c.c_size_t),
        ("enable_eraser", c.c_uint32),
        ("enable_hover", c.c_uint32),
    ]


class BatteryOptions(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("report_id", ReportId),
        ("strength", IntegerField),
        ("enable_unknown_null_state", c.c_uint32),
    ]


class RawReport(c.Structure):
    _fields_ = [
        ("report_id", c.c_uint8),
        ("has_report_id", c.c_uint8),
        ("reserved16", c.c_uint16),
        ("wire_length", c.c_uint32),
    ]


class RawOptions(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("descriptor", c.POINTER(c.c_uint8)),
        ("descriptor_length", c.c_size_t),
        ("reports", c.POINTER(RawReport)),
        ("report_count", c.c_size_t),
        ("acknowledges_no_android_support", c.c_uint32),
        ("requires_output", c.c_uint32),
        ("requires_feature_response", c.c_uint32),
    ]


class ReportCapability(c.Structure):
    _fields_ = [
        ("report_id", c.c_uint8),
        ("has_report_id", c.c_uint8),
        ("reserved16", c.c_uint16),
        ("wire_length", c.c_uint32),
    ]


class CapabilityManifest(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("input_supported", c.c_uint32),
        ("output_supported", c.c_uint32),
        ("feature_transport_supported", c.c_uint32),
        ("android_status", c.c_int32),
        ("profile_kind", c.c_int32),
        ("reports", c.POINTER(ReportCapability)),
        ("report_count", c.c_size_t),
        ("descriptor_bytes", c.c_size_t),
    ]


class TouchContact(c.Structure):
    _fields_ = [
        ("contact_id", c.c_uint32),
        ("x", c.c_int32),
        ("y", c.c_int32),
        ("pressure", c.c_int32),
        ("width", c.c_int32),
        ("height", c.c_int32),
        ("azimuth", c.c_int32),
    ]


class TouchExtra(c.Structure):
    _fields_ = [
        ("pressure", c.c_int32),
        ("width", c.c_int32),
        ("height", c.c_int32),
        ("azimuth", c.c_int32),
    ]


class PenSample(c.Structure):
    _fields_ = [
        ("in_range", c.c_uint32),
        ("tip", c.c_uint32),
        ("eraser", c.c_uint32),
        ("barrel_buttons", c.c_uint32),
        ("x", c.c_int32),
        ("y", c.c_int32),
        ("pressure", c.c_int32),
        ("tilt_x", c.c_int32),
        ("tilt_y", c.c_int32),
        ("twist", c.c_int32),
    ]


ABI_STRUCTS = {
    "aoahid_error_detail": ErrorDetail,
    "aoahid_context_options": ContextOptions,
    "aoahid_device_info": DeviceInfo,
    "aoahid_aoa_strings": AoaStrings,
    "aoahid_device_options": DeviceOptions,
    "aoahid_accessory_options": AccessoryOptions,
    "aoahid_channel_options": ChannelOptions,
    "aoahid_node_options": NodeOptions,
    "aoahid_physical_properties": PhysicalProperties,
    "aoahid_integer_field": IntegerField,
    "aoahid_report_id_option": ReportId,
    "aoahid_keyboard_options": KeyboardOptions,
    "aoahid_mouse_options": MouseOptions,
    "aoahid_toggle_options": ToggleOptions,
    "aoahid_gamepad_axis": GamepadAxis,
    "aoahid_gamepad_options": GamepadOptions,
    "aoahid_touchscreen_options": TouchscreenOptions,
    "aoahid_touchpad_options": TouchpadOptions,
    "aoahid_pen_options": PenOptions,
    "aoahid_battery_options": BatteryOptions,
    "aoahid_raw_report": RawReport,
    "aoahid_raw_options": RawOptions,
    "aoahid_report_capability": ReportCapability,
    "aoahid_capability_manifest": CapabilityManifest,
    "aoahid_touch_contact": TouchContact,
    "aoahid_touch_extra": TouchExtra,
    "aoahid_pen_sample": PenSample,
}


def load(path: Union[os.PathLike, str]) -> c.CDLL:
    """Load exactly the caller-selected shared library and declare every symbol."""
    library = c.CDLL(os.fspath(path))

    def declare(name: str, arguments: list[object], result: object) -> None:
        function = getattr(library, name)
        function.argtypes = arguments
        function.restype = result

    result = c.c_int32
    declare("aoahid_last_error", [], c.POINTER(ErrorDetail))
    declare("aoahid_result_name", [result], c.c_char_p)
    declare("aoahid_version", [], c.c_uint32)
    declare("aoahid_context_create", [c.POINTER(ContextOptions), c.POINTER(ContextP)], result)
    declare("aoahid_context_poll", [ContextP, c.c_uint32], result)
    declare("aoahid_context_destroy", [ContextP], result)
    declare("aoahid_context_destroy_blocking", [ContextP, c.c_uint32], result)
    declare("aoahid_discover", [ContextP, c.c_uint32, c.POINTER(DiscoveryP)], result)
    declare("aoahid_discovery_count", [DiscoveryP], c.c_size_t)
    declare("aoahid_discovery_get", [DiscoveryP, c.c_size_t], c.POINTER(DeviceInfo))
    declare("aoahid_discovery_destroy", [DiscoveryP], None)
    declare("aoahid_accessory_start", [ContextP, c.POINTER(DeviceInfo), c.POINTER(AccessoryOptions)], result)
    declare("aoahid_device_open", [ContextP, c.POINTER(DeviceInfo), c.POINTER(DeviceOptions), c.POINTER(DeviceP)], result)
    declare("aoahid_device_close", [DeviceP], result)
    declare("aoahid_device_latched_error", [DeviceP], result)
    declare("aoahid_channel_open", [DeviceP, c.POINTER(ChannelOptions), c.POINTER(ChannelP)], result)
    declare("aoahid_channel_close", [ChannelP], result)
    declare("aoahid_channel_write", [ChannelP, c.POINTER(c.c_uint8), c.c_size_t, c.POINTER(c.c_size_t), c.c_uint32], result)
    declare("aoahid_channel_read", [ChannelP, c.POINTER(c.c_uint8), c.c_size_t, c.POINTER(c.c_size_t), c.c_uint32], result)

    factories = {
        "aoahid_spec_create_keyboard": KeyboardOptions,
        "aoahid_spec_create_mouse": MouseOptions,
        "aoahid_spec_create_toggle": ToggleOptions,
        "aoahid_spec_create_gamepad": GamepadOptions,
        "aoahid_spec_create_touchscreen": TouchscreenOptions,
        "aoahid_spec_create_touchpad": TouchpadOptions,
        "aoahid_spec_create_pen": PenOptions,
        "aoahid_spec_create_battery": BatteryOptions,
        "aoahid_spec_create_raw": RawOptions,
    }
    for name, option_type in factories.items():
        declare(name, [c.POINTER(option_type), c.POINTER(SpecP)], result)
    declare("aoahid_spec_retain", [SpecP], None)
    declare("aoahid_spec_release", [SpecP], None)
    declare("aoahid_spec_descriptor", [SpecP, c.POINTER(c.POINTER(c.c_uint8)), c.POINTER(c.c_size_t)], result)
    declare("aoahid_spec_manifest", [SpecP, c.POINTER(CapabilityManifest)], result)

    declare("aoahid_node_open", [DeviceP, SpecP, c.POINTER(NodeOptions), c.POINTER(NodeP)], result)
    declare("aoahid_node_close", [NodeP], result)
    declare("aoahid_node_hid_id", [NodeP], c.c_uint16)
    declare("aoahid_node_manifest", [NodeP, c.POINTER(CapabilityManifest)], result)
    declare("aoahid_node_submit", [NodeP], result)
    declare("aoahid_node_submit_blocking", [NodeP, c.c_uint32], result)
    declare("aoahid_kbd", [NodeP, c.c_uint16, c.c_uint32], result)
    declare("aoahid_mouse_move", [NodeP, c.c_int32, c.c_int32], result)
    declare("aoahid_mouse_scroll", [NodeP, c.c_int32, c.c_int32], result)
    declare("aoahid_mouse_button", [NodeP, c.c_uint32, c.c_uint32], result)
    declare("aoahid_toggle", [NodeP, c.c_uint16, c.c_uint32], result)
    declare("aoahid_gamepad_button", [NodeP, c.c_uint32, c.c_uint32], result)
    declare("aoahid_gamepad_set_axis", [NodeP, c.c_size_t, c.c_int32], result)
    declare("aoahid_dpad", [NodeP, c.c_uint32, c.c_uint32, c.c_uint32, c.c_uint32], result)
    declare("aoahid_touch", [NodeP, c.c_uint32, c.c_uint32, c.c_int32, c.c_int32, c.POINTER(TouchExtra)], result)
    declare("aoahid_touchpad_button", [NodeP, c.c_uint32, c.c_uint32], result)
    declare("aoahid_pen_update", [NodeP, c.POINTER(PenSample)], result)
    declare("aoahid_pen_depart", [NodeP], result)
    declare("aoahid_battery_update", [NodeP, c.c_uint32, c.c_int32], result)
    declare("aoahid_raw_submit", [NodeP, c.POINTER(c.c_uint8), c.c_size_t], result)
    return library


# Keep ``from aoahid import *`` limited to the stable binding surface instead
# of leaking ctypes, os, or typing implementation details from this module.
__all__ = [name for name in globals() if name.startswith("AOAHID_")]
__all__ += [
    "Context",
    "Discovery",
    "Device",
    "Spec",
    "Node",
    "ContextP",
    "DiscoveryP",
    "DeviceP",
    "SpecP",
    "NodeP",
    "Channel",
    "ChannelP",
    "ChannelOptions",
    "LogSink",
    "ErrorDetail",
    "ContextOptions",
    "DeviceInfo",
    "AoaStrings",
    "DeviceOptions",
    "AccessoryOptions",
    "NodeOptions",
    "PhysicalProperties",
    "IntegerField",
    "ReportId",
    "KeyboardOptions",
    "MouseOptions",
    "ToggleOptions",
    "GamepadAxis",
    "GamepadOptions",
    "TouchscreenOptions",
    "TouchpadOptions",
    "PenOptions",
    "BatteryOptions",
    "RawReport",
    "RawOptions",
    "ReportCapability",
    "CapabilityManifest",
    "TouchContact",
    "TouchExtra",
    "PenSample",
    "ABI_STRUCTS",
    "load",
]
