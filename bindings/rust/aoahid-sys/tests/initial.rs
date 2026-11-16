// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
//! `Default` of the option structures must equal what the compiled C `aoahid_*_init` functions
//! leave, byte for byte. AOAHID_INIT_ORACLE names the built aoahid_abi_init_oracle program;
//! pointer fields are compared by being null or not, and by what they point at.

use aoahid_sys::*;
use core::mem::{offset_of, size_of};
use std::collections::BTreeMap;
use std::ffi::CStr;
use std::process::Command;

fn bytes_of<T>(value: &T) -> Vec<u8> {
    // SAFETY: a plain repr(C) structure read as bytes; padding is zero because Default starts from zero.
    unsafe { core::slice::from_raw_parts(value as *const T as *const u8, size_of::<T>()).to_vec() }
}

fn hex(text: &str) -> Vec<u8> {
    (0..text.len() / 2).map(|i| u8::from_str_radix(&text[2 * i..2 * i + 2], 16).unwrap()).collect()
}

/// Replaces each pointer field with 0 or 1 by whether it is null, so addresses do not matter.
fn masked(mut data: Vec<u8>, pointers: &[usize]) -> Vec<u8> {
    for &offset in pointers {
        let width = size_of::<usize>();
        let set = data[offset..offset + width].iter().any(|&b| b != 0);
        data[offset..offset + width].fill(0);
        data[offset] = set as u8;
    }
    data
}

#[test]
fn default_equals_the_c_initializers() {
    let path = std::env::var("AOAHID_INIT_ORACLE").expect("AOAHID_INIT_ORACLE is not set");
    let output = Command::new(path).output().expect("run the oracle");
    let text = String::from_utf8(output.stdout).unwrap();
    let lines: BTreeMap<&str, &str> = text.lines().filter_map(|l| l.split_once('=')).collect();

    macro_rules! check {
        ($name:literal, $type:ty, [$($pointer:expr),*]) => {{
            let pointers: &[usize] = &[$($pointer),*];
            let value = <$type>::default();
            assert_eq!(
                masked(bytes_of(&value), pointers),
                masked(hex(lines[$name]), pointers),
                "{}", $name
            );
        }};
    }
    check!("context_options", aoahid_context_options, [offset_of!(aoahid_context_options, log_sink), offset_of!(aoahid_context_options, log_user)]);
    check!("device_options", aoahid_device_options, []);
    check!("accessory_options", aoahid_accessory_options, [
        offset_of!(aoahid_accessory_options, strings) + offset_of!(aoahid_aoa_strings, manufacturer),
        offset_of!(aoahid_accessory_options, strings) + offset_of!(aoahid_aoa_strings, model)]);
    check!("channel_options", aoahid_channel_options, []);
    check!("keyboard_options", aoahid_keyboard_options, []);
    check!("mouse_options", aoahid_mouse_options, []);
    check!("toggle_options", aoahid_toggle_options, [
        offset_of!(aoahid_toggle_options, allowed_usages), offset_of!(aoahid_toggle_options, usage_semantics),
        offset_of!(aoahid_toggle_options, expected_linux_event_types), offset_of!(aoahid_toggle_options, expected_linux_codes)]);
    check!("gamepad_options", aoahid_gamepad_options, [offset_of!(aoahid_gamepad_options, axes)]);
    check!("touchscreen_options", aoahid_touchscreen_options, []);
    check!("touchpad_options", aoahid_touchpad_options, []);
    check!("pen_options", aoahid_pen_options, [offset_of!(aoahid_pen_options, barrel_usages)]);
    check!("digitizer_options", aoahid_digitizer_options, [offset_of!(aoahid_digitizer_options, barrel_usages)]);
    // The zero-only initializers.
    check!("node_options", aoahid_node_options, []);
    check!("raw_options", aoahid_raw_options, []);

    let gamepad = aoahid_gamepad_options::default();
    for index in 0..gamepad.axis_count {
        // SAFETY: Default points `axes` at a static table of `axis_count` entries.
        let axis = unsafe { &*gamepad.axes.add(index) };
        let (code, android) = unsafe { (CStr::from_ptr(axis.expected_linux_code), CStr::from_ptr(axis.expected_android_axis)) };
        let seen = format!("{},{},{},{},{},{},{},{},{}", axis.role, axis.usage_page, axis.usage,
            axis.value.logical_minimum, axis.value.logical_maximum, axis.value.bit_width,
            axis.neutral_value, code.to_str().unwrap(), android.to_str().unwrap());
        assert_eq!(lines[format!("gamepad_axis{index}").as_str()], seen);
    }
    let pen = aoahid_pen_options::default();
    // SAFETY: Default points `barrel_usages` at a static array of `barrel_usage_count` entries.
    let barrel = unsafe { *pen.barrel_usages };
    assert_eq!(lines["pen_barrel"], format!("{},{}", pen.barrel_usage_count, barrel));
    let strings = aoahid_accessory_options::default().strings;
    let (manufacturer, model) = unsafe { (CStr::from_ptr(strings.manufacturer), CStr::from_ptr(strings.model)) };
    assert_eq!(lines["accessory"], format!("{},{}", manufacturer.to_str().unwrap(), model.to_str().unwrap()));
}
