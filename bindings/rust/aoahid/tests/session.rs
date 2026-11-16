// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
//! Context, Device and Node against the fake libusb build of the shared library
//! (AOAHID_USE_FAKE_LIBUSB=ON, the library CI builds for the bindings). No phone is needed.

use aoahid::{Context, OpenError};
use aoahid_sys as sys;
use core::ffi::c_char;
use std::time::Duration;

#[repr(C)]
struct FakeDevice {
    bus: u8,
    address: u8,
    port_path: *const u8,
    port_path_length: usize,
    vendor_id: u16,
    product_id: u16,
    protocol_version: u16,
    serial: *const c_char,
    product: *const c_char,
    open_status: i32,
}

extern "C" {
    fn aoahid_fake_libusb_reset();
    fn aoahid_fake_libusb_add_device(config: *const FakeDevice) -> i32;
}

fn add_phone() {
    static PATH: [u8; 2] = [3, 4];
    let phone = FakeDevice {
        bus: 1,
        address: 5,
        port_path: PATH.as_ptr(),
        port_path_length: 2,
        vendor_id: 0x18D1,
        product_id: 0x2D00,
        protocol_version: 2,
        serial: c"fake-serial".as_ptr(),
        product: c"Android".as_ptr(),
        open_status: 0,
    };
    unsafe {
        aoahid_fake_libusb_reset();
        assert!(aoahid_fake_libusb_add_device(&phone) >= 0);
    }
}

// One test function: the fake keeps its devices in process-wide state.
#[test]
fn owning_wrappers_open_send_and_release() {
    let context = Context::new().expect("context");
    unsafe { aoahid_fake_libusb_reset() };
    assert_eq!(context.open_only().err(), Some(OpenError::DeviceCount(0)));

    add_phone();
    {
        let device = context.open_only().expect("exactly one device");
        let keyboard = device.keyboard(&Default::default(), Duration::ZERO).expect("keyboard");
        keyboard.key(0x04, true).unwrap();
        keyboard.submit_blocking(sys::AOAHID_SEND_TIMEOUT_MS).unwrap();
        keyboard.key(0x04, false).unwrap();
        keyboard.submit_blocking(sys::AOAHID_SEND_TIMEOUT_MS).unwrap();
        let mouse = device.mouse(&Default::default(), Duration::ZERO).expect("mouse");
        mouse.move_by(5, -5).unwrap();
        mouse.submit_blocking(sys::AOAHID_SEND_TIMEOUT_MS).unwrap();
        // SAFETY: the default gamepad options point at statics.
        let gamepad = unsafe { device.gamepad(&Default::default(), Duration::ZERO) }.expect("gamepad");
        gamepad.axis(0, 100).unwrap();
        gamepad.submit_blocking(sys::AOAHID_SEND_TIMEOUT_MS).unwrap();
    } // Nodes, then the Device, are released here.

    // An options structure that the library refuses comes back as its result code.
    add_phone();
    let device = context.open_only().expect("device");
    let bad = sys::aoahid_keyboard_options { usage_minimum: 0, ..Default::default() };
    assert!(device.keyboard(&bad, Duration::ZERO).is_err());
}
