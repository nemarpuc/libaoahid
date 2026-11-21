// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
//! Presses and releases the letter "a" on the first Android phone found.
//!
//! Open a text field on the phone first. Every value below is the program's own choice; none is a
//! default of the library (the limits they sit within are in docs/LIMITS.md). The phone must
//! already take AOA 2.0 HID requests; docs/QUICKSTART.md says how to get there and what to do when
//! the first aoahid_node_open fails with AOAHID_ERR_STALL.

use aoahid_sys::*;
use core::ptr;
use std::ffi::CStr;
use std::{thread, time::Duration};

const KEY_A: u16 = 0x04; // HID Keyboard page usage
const TIMEOUT_MS: u32 = 500;

/// A zeroed structure with only struct_size set, as the C `*_init` functions do.
macro_rules! init {
    ($type:ty) => {{
        // SAFETY: every field of these repr(C) structures is an integer, an array of integers,
        // a raw pointer or an Option of a function pointer, for all of which zero is valid.
        let mut value: $type = unsafe { core::mem::zeroed() };
        value.struct_size = core::mem::size_of::<$type>() as u32;
        value
    }};
}

/// Turns a result other than AOAHID_OK into the library's own words for it. Call it right after
/// the call: the details belong to the calling thread's last result.
fn check(result: aoahid_result) -> Result<(), String> {
    if result == AOAHID_OK {
        return Ok(());
    }
    let text = |raw: *const core::ffi::c_char| {
        if raw.is_null() {
            String::new()
        } else {
            unsafe { CStr::from_ptr(raw) }.to_string_lossy().into_owned()
        }
    };
    let detail = unsafe { &*aoahid_last_error() };
    let (field, reason) = (text(detail.field), text(detail.reason));
    let name = text(unsafe { aoahid_result_name(result) });
    Err(format!("{name}: {field}: {reason}"))
}

fn main() {
    let mut context_options = init!(aoahid_context_options);
    context_options.event_mode = AOAHID_EVENT_CALLER_POLL;
    context_options.log_level = AOAHID_LOG_DISABLED;

    let mut device_options = init!(aoahid_device_options);
    device_options.descriptor_fragment_bytes = 4096;
    device_options.transfer_pool_slots = 4;
    device_options.maximum_report_bytes = 1024;
    device_options.aoa_descriptor_wire_policy_bytes = 4096;
    device_options.linux_hid.descriptor_bytes = 4096;
    device_options.linux_hid.fields_per_report = 256;
    device_options.linux_hid.global_stack_depth = 4;
    device_options.linux_hid.report_size_bits = 256;
    device_options.linux_hid.usages = 12288;
    device_options.linux_hid.report_data_bits = 65528;
    device_options.target_ep0_data_policy_bytes = 4096;
    device_options.host_control_buffer_policy_bytes = 4096;
    device_options.interface_claim_policy = AOAHID_INTERFACE_CLAIM_NONE;
    device_options.interface_number = -1;

    let mut keyboard = init!(aoahid_keyboard_options);
    keyboard.usage_minimum = 0x04;
    keyboard.usage_maximum = 0xDD;

    let mut placement = init!(aoahid_node_placement); // no reserved transfer slot
    placement.registration = 1; // its own HID device
    let mut timeouts = init!(aoahid_commit_timeouts);
    timeouts.control_timeout_ms = TIMEOUT_MS;
    timeouts.drain_timeout_ms = TIMEOUT_MS;

    let mut context = ptr::null_mut();
    let mut discovery = ptr::null_mut();
    let mut device = ptr::null_mut();
    let mut spec = ptr::null_mut();
    let mut node = ptr::null_mut();
    let outcome = (|| unsafe {
        check(aoahid_context_create(&context_options, &mut context))?;
        check(aoahid_discover(context, TIMEOUT_MS, &mut discovery))?;
        if aoahid_discovery_count(discovery) == 0 {
            return Err("no USB device found".to_string());
        }
        let info = aoahid_discovery_get(discovery, 0);
        check(aoahid_device_open(context, info, &device_options, &mut device))?;
        check(aoahid_spec_create_keyboard(&keyboard, &mut spec))?;
        check(aoahid_node_open(device, spec, &placement, &timeouts, &mut node))?;
        // Android registers the HID without telling the host; a report sent at once may be lost.
        thread::sleep(Duration::from_secs(1));
        for down in [1, 0] {
            check(aoahid_keyboard_key(node, KEY_A, down))?;
            check(aoahid_node_submit_blocking(node, TIMEOUT_MS))?;
            thread::sleep(Duration::from_millis(50));
        }
        Ok(())
    })();
    unsafe {
        if !discovery.is_null() {
            aoahid_discovery_destroy(discovery);
        }
        if !spec.is_null() {
            aoahid_spec_release(spec);
        }
        if !node.is_null() {
            aoahid_node_close(node, &timeouts);
        }
        if !device.is_null() {
            aoahid_device_close(device, TIMEOUT_MS, TIMEOUT_MS);
        }
        if !context.is_null() {
            aoahid_context_destroy_blocking(context, TIMEOUT_MS, TIMEOUT_MS, TIMEOUT_MS);
        }
    }
    if let Err(message) = outcome {
        eprintln!("{message}");
        std::process::exit(1);
    }
}
