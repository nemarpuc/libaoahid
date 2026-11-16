# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libaoahid contributors
"""Option structures filled with the values of the C ``aoahid_*_init`` functions of ``aoahid.h``.

Each function returns a plain ctypes structure; keyword arguments replace single fields (a nested
field takes the structure itself, e.g. ``x=integer_field(0, 255, 8)``). tests/test_initial.py
compares every structure byte for byte with what the compiled C initializers produce, so the two
cannot drift apart. Pointer targets live in this module and stay valid for the life of the process.
"""

from __future__ import annotations

import ctypes as c

from . import native as n

__all__ = [
    "integer_field", "context_options", "device_options", "accessory_options", "channel_options",
    "node_options", "keyboard_options", "mouse_options", "toggle_options", "gamepad_options",
    "touchscreen_options", "touchpad_options", "pen_options", "digitizer_options", "raw_options",
]


def integer_field(logical_minimum: int, logical_maximum: int, bit_width: int) -> n.IntegerField:
    """The C ``aoahid_integer_field_make``: a field with no physical properties."""
    field = n.IntegerField()
    field.logical_minimum = logical_minimum
    field.logical_maximum = logical_maximum
    field.bit_width = bit_width
    return field


def _build(cls: type, values: dict, overrides: dict):
    options = cls()
    for name, value in {**values, **overrides}.items():
        setattr(options, name, value)
    return options


def context_options(**overrides):
    return _build(n.ContextOptions, {"event_mode": n.AOAHID_EVENT_INTERNAL_THREAD,
                                     "log_level": n.AOAHID_LOG_DISABLED}, overrides)


def device_options(**overrides):
    policy = n.LinuxHidPolicy(4096, 256, 4, 256, 12288, 65528)
    return _build(n.DeviceOptions, {
        "descriptor_fragment_bytes": 4096, "transfer_pool_slots": 4, "maximum_report_bytes": 1024,
        "aoa_descriptor_wire_policy_bytes": 4096, "linux_hid": policy,
        "target_ep0_data_policy_bytes": 4096, "host_control_buffer_policy_bytes": 4096,
        "interface_claim_policy": n.AOAHID_INTERFACE_CLAIM_NONE, "interface_number": -1}, overrides)


def accessory_options(**overrides):
    options = n.AccessoryOptions()
    options.strings.manufacturer = b"libaoahid"
    options.strings.model = b"libaoahid"
    return _build(n.AccessoryOptions, {"strings": options.strings}, overrides)


def channel_options(**overrides):
    return _build(n.ChannelOptions, {
        "interface_class": 0xFF, "interface_subclass": 0x42, "interface_protocol": 0x01,
        "out_transfers": 2, "transfer_bytes": 1048576, "zero_length_termination": 1,
        "read_mode": n.AOAHID_CHANNEL_READ_REQUEST}, overrides)


def node_options(**overrides):
    return _build(n.NodeOptions, {}, overrides)


def raw_options(**overrides):
    return _build(n.RawOptions, {}, overrides)


def keyboard_options(**overrides):
    return _build(n.KeyboardOptions, {"usage_minimum": 0x04, "usage_maximum": 0xDD}, overrides)


def mouse_options(**overrides):
    return _build(n.MouseOptions, {
        "button_count": 5, "x": integer_field(-32767, 32767, 16), "y": integer_field(-32767, 32767, 16),
        "enable_wheel": 1, "wheel": integer_field(-127, 127, 8),
        "enable_pan": 1, "pan": integer_field(-127, 127, 8)}, overrides)


def toggle_options(**overrides):
    """The keys themselves (allowed_usages and the arrays beside it) are the caller's to list."""
    return _build(n.ToggleOptions, {"application_page": 0x0C, "application_usage": 0x01,
                                    "field_page": 0x0C}, overrides)


def _axis(role: int, usage: int, code: bytes, android: bytes) -> n.GamepadAxisOption:
    axis = n.GamepadAxisOption()
    axis.role, axis.usage_page, axis.usage = role, 0x01, usage
    axis.value = integer_field(-127, 127, 8)
    axis.expected_linux_code, axis.expected_android_axis = code, android
    return axis


_GAMEPAD_AXES = (n.GamepadAxisOption * 4)(
    _axis(n.AOAHID_AXIS_X, 0x30, b"ABS_X", b"AXIS_X"), _axis(n.AOAHID_AXIS_Y, 0x31, b"ABS_Y", b"AXIS_Y"),
    _axis(n.AOAHID_AXIS_Z, 0x32, b"ABS_Z", b"AXIS_Z"), _axis(n.AOAHID_AXIS_RZ, 0x35, b"ABS_RZ", b"AXIS_RZ"))
_BARREL_USAGES = (c.c_uint16 * 1)(0x44)


def gamepad_options(**overrides):
    return _build(n.GamepadOptions, {
        "axes": c.cast(_GAMEPAD_AXES, c.POINTER(n.GamepadAxisOption)), "axis_count": 4,
        "button_count": 12, "button_usage_minimum": 1, "dpad_representation": n.AOAHID_DPAD_HAT,
        "hat_logical_minimum": 0, "hat_logical_maximum": 7, "hat_bit_width": 4}, overrides)


def touchscreen_options(**overrides):
    return _build(n.TouchscreenOptions, {
        "maximum_contacts": 10, "contacts_per_report": 10, "contact_id": integer_field(0, 15, 4),
        "x": integer_field(0, 32767, 16), "y": integer_field(0, 32767, 16),
        "contact_count": integer_field(0, 10, 4)}, overrides)


def touchpad_options(**overrides):
    return _build(n.TouchpadOptions, {
        "maximum_contacts": 5, "contacts_per_report": 5, "contact_id": integer_field(0, 15, 4),
        "x": integer_field(0, 32767, 16), "y": integer_field(0, 32767, 16),
        "contact_count": integer_field(0, 5, 4), "button_count": 1}, overrides)


def _pen_values() -> dict:
    return {"x": integer_field(0, 32767, 16), "y": integer_field(0, 32767, 16), "enable_pressure": 1,
            "pressure": integer_field(0, 4095, 12),
            "barrel_usages": c.cast(_BARREL_USAGES, c.POINTER(c.c_uint16)), "barrel_usage_count": 1,
            "enable_hover": 1}


def pen_options(**overrides):
    return _build(n.PenOptions, _pen_values(), overrides)


def digitizer_options(**overrides):
    return _build(n.DigitizerOptions, _pen_values(), overrides)
