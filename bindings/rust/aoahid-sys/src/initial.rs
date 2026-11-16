// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
// The values of the C `aoahid_*_init` functions of aoahid.h, for the structures whose initializer
// fills values (HAND_INITIAL in tools/bindings/generate.py). tests/initial.rs compares each with
// what the compiled C initializer leaves, so the two cannot drift apart. The pointer targets are
// statics, valid for the life of the process.

use crate::*;

fn sized<T>() -> T {
    // SAFETY: the structures here hold integers, pointers and optional function pointers, and
    // all-zero bytes are valid for each of them.
    let mut value: T = unsafe { core::mem::zeroed() };
    // SAFETY: every structure that calls this starts with a `u32` struct_size.
    unsafe { *(&mut value as *mut T as *mut u32) = core::mem::size_of::<T>() as u32 };
    value
}

/// The C `aoahid_integer_field_make`: no physical properties.
pub fn integer_field(logical_minimum: i32, logical_maximum: i32, bit_width: u32) -> aoahid_integer_field {
    // SAFETY: all-zero bytes are valid for an integer field.
    let mut field: aoahid_integer_field = unsafe { core::mem::zeroed() };
    field.logical_minimum = logical_minimum;
    field.logical_maximum = logical_maximum;
    field.bit_width = bit_width;
    field
}

fn copy_field(field: &aoahid_integer_field) -> aoahid_integer_field {
    integer_field(field.logical_minimum, field.logical_maximum, field.bit_width)
}

impl Default for aoahid_context_options {
    /// An internal event thread and no logging.
    fn default() -> Self {
        Self { event_mode: AOAHID_EVENT_INTERNAL_THREAD, log_level: AOAHID_LOG_DISABLED, ..sized() }
    }
}

impl Default for aoahid_device_options {
    /// 4096-byte buffers, 4 transfer slots, 1024-byte reports, no interface claim, and the Linux
    /// HID parser limits of docs/LIMITS.md.
    fn default() -> Self {
        Self {
            descriptor_fragment_bytes: 4096,
            transfer_pool_slots: 4,
            maximum_report_bytes: 1024,
            aoa_descriptor_wire_policy_bytes: 4096,
            linux_hid: aoahid_linux_hid_policy {
                descriptor_bytes: 4096,
                fields_per_report: 256,
                global_stack_depth: 4,
                report_size_bits: 256,
                usages: 12288,
                report_data_bits: 65528,
            },
            target_ep0_data_policy_bytes: 4096,
            host_control_buffer_policy_bytes: 4096,
            interface_claim_policy: AOAHID_INTERFACE_CLAIM_NONE,
            interface_number: -1,
            ..sized()
        }
    }
}

impl Default for aoahid_accessory_options {
    /// Manufacturer and model "libaoahid"; set your own when you ship an accessory.
    fn default() -> Self {
        let mut value: Self = sized();
        value.strings.manufacturer = c"libaoahid".as_ptr();
        value.strings.model = c"libaoahid".as_ptr();
        value
    }
}

impl Default for aoahid_channel_options {
    /// The ADB interface, read on request, 1 MiB transfers, two OUT transfers, zero-length packets.
    fn default() -> Self {
        Self {
            interface_class: 0xFF,
            interface_subclass: 0x42,
            interface_protocol: 0x01,
            out_transfers: 2,
            transfer_bytes: 1_048_576,
            zero_length_termination: 1,
            read_mode: AOAHID_CHANNEL_READ_REQUEST,
            ..sized()
        }
    }
}

impl Default for aoahid_keyboard_options {
    /// Every key from Keyboard a (0x04) to Keypad Hexadecimal (0xDD).
    fn default() -> Self {
        Self { usage_minimum: 0x04, usage_maximum: 0xDD, ..sized() }
    }
}

impl Default for aoahid_mouse_options {
    /// Five buttons, 16-bit X and Y, and an 8-bit wheel and pan.
    fn default() -> Self {
        Self {
            button_count: 5,
            x: integer_field(-32767, 32767, 16),
            y: integer_field(-32767, 32767, 16),
            enable_wheel: 1,
            wheel: integer_field(-127, 127, 8),
            enable_pan: 1,
            pan: integer_field(-127, 127, 8),
            ..sized()
        }
    }
}

impl Default for aoahid_toggle_options {
    /// A Consumer Control collection; the keys themselves are the caller's to list.
    fn default() -> Self {
        Self { application_page: 0x0C, application_usage: 0x01, field_page: 0x0C, ..sized() }
    }
}

struct Axes([aoahid_gamepad_axis_option; 4]);
// SAFETY: read-only, and the pointers inside point at string literals.
unsafe impl Sync for Axes {}

const fn axis(role: i32, usage: u16, code: *const core::ffi::c_char, android: *const core::ffi::c_char)
    -> aoahid_gamepad_axis_option {
    aoahid_gamepad_axis_option {
        role,
        usage_page: 0x01,
        usage,
        value: aoahid_integer_field {
            logical_minimum: -127,
            logical_maximum: 127,
            bit_width: 8,
            physical: aoahid_physical_properties { enabled: 0, minimum: 0, maximum: 0, unit_exponent: 0, unit: 0 },
        },
        neutral_value: 0,
        expected_linux_code: code,
        expected_android_axis: android,
    }
}

static GAMEPAD_AXES: Axes = Axes([
    axis(AOAHID_AXIS_X, 0x30, c"ABS_X".as_ptr(), c"AXIS_X".as_ptr()),
    axis(AOAHID_AXIS_Y, 0x31, c"ABS_Y".as_ptr(), c"AXIS_Y".as_ptr()),
    axis(AOAHID_AXIS_Z, 0x32, c"ABS_Z".as_ptr(), c"AXIS_Z".as_ptr()),
    axis(AOAHID_AXIS_RZ, 0x35, c"ABS_RZ".as_ptr(), c"AXIS_RZ".as_ptr()),
]);

impl Default for aoahid_gamepad_options {
    /// Two sticks (X/Y and Z/Rz), twelve buttons from Button 1, and an eight-way Hat.
    fn default() -> Self {
        Self {
            axes: GAMEPAD_AXES.0.as_ptr(),
            axis_count: 4,
            button_count: 12,
            button_usage_minimum: 1,
            dpad_representation: AOAHID_DPAD_HAT,
            hat_logical_minimum: 0,
            hat_logical_maximum: 7,
            hat_bit_width: 4,
            ..sized()
        }
    }
}

impl Default for aoahid_touchscreen_options {
    /// Ten contacts in one report, X and Y from 0 to 32767, and no optional field.
    fn default() -> Self {
        Self {
            maximum_contacts: 10,
            contacts_per_report: 10,
            contact_id: integer_field(0, 15, 4),
            x: integer_field(0, 32767, 16),
            y: integer_field(0, 32767, 16),
            contact_count: integer_field(0, 10, 4),
            ..sized()
        }
    }
}

impl Default for aoahid_touchpad_options {
    /// Five contacts in one report, X and Y from 0 to 32767, no optional field, one click button.
    fn default() -> Self {
        Self {
            maximum_contacts: 5,
            contacts_per_report: 5,
            contact_id: integer_field(0, 15, 4),
            x: integer_field(0, 32767, 16),
            y: integer_field(0, 32767, 16),
            contact_count: integer_field(0, 5, 4),
            button_count: 1,
            ..sized()
        }
    }
}

static BARREL_USAGES: [u16; 1] = [0x44];

impl Default for aoahid_pen_options {
    /// X and Y from 0 to 32767, 12-bit pressure, one Barrel Switch, and hover.
    fn default() -> Self {
        Self {
            x: integer_field(0, 32767, 16),
            y: integer_field(0, 32767, 16),
            enable_pressure: 1,
            pressure: integer_field(0, 4095, 12),
            barrel_usages: BARREL_USAGES.as_ptr(),
            barrel_usage_count: 1,
            enable_hover: 1,
            ..sized()
        }
    }
}

impl Default for aoahid_digitizer_options {
    /// The same values as the pen.
    fn default() -> Self {
        let pen = aoahid_pen_options::default();
        Self {
            x: copy_field(&pen.x),
            y: copy_field(&pen.y),
            enable_pressure: pen.enable_pressure,
            pressure: copy_field(&pen.pressure),
            barrel_usages: pen.barrel_usages,
            barrel_usage_count: pen.barrel_usage_count,
            enable_hover: pen.enable_hover,
            ..sized()
        }
    }
}
