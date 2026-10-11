// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
//! Exercises caller-owned discovery, multi-profile state, reconnection, and
//! shutdown through the literal Rust FFI. The binding provides no option values.
//! `docs/EXAMPLES.md` explains the Usage and protocol choices.

use aoahid_sys as sys;
use core::ffi::CStr;
use core::ptr;
use std::slice;
use std::thread;

// Every value in this block is this caller's reviewed example policy or input.
// None is selected by libaoahid; docs/EXAMPLES.md lists the example values.
const DESCRIPTOR_POLICY_BYTES: u32 = 4096;
const HOST_REPORT_POLICY_BYTES: u32 = 4088;
const POOL_SLOTS: u32 = 8;
const RESERVED_SLOTS_PER_NODE: u32 = 1;
const CLOSE_DRAIN_TIMEOUT_MS: u32 = 1000;
const KEYBOARD_A_USAGE: u16 = 0x04; // HUT 1.7 section 10.
const KEYBOARD_APPLICATION_USAGE: u16 = 0x65; // HUT 1.7 section 10.
const POINTER_BUTTON_COUNT: u32 = 3;
const RELATIVE_MINIMUM: i32 = -127;
const RELATIVE_MAXIMUM: i32 = 127;
const RELATIVE_BITS: u32 = 8;
const TOUCH_CONTACT_ID: u32 = 1;
const TOUCH_CONTACT_ID_MAXIMUM: i32 = 15;
const TOUCH_CONTACT_ID_BITS: u32 = 4;
const TOUCH_COORDINATE_MAXIMUM: i32 = 32767;
const TOUCH_COORDINATE_BITS: u32 = 16;
const TOUCH_CONTACT_COUNT_BITS: u32 = 1;
const EXAMPLE_TOUCH_X: i32 = 1000;
const EXAMPLE_TOUCH_Y: i32 = 2000;
const EXAMPLE_MOUSE_DX: i32 = 30;
const EXAMPLE_MOUSE_DY: i32 = -20;

#[derive(Clone, Eq, PartialEq)]
struct Locator {
    bus_number: u8,
    port_path: Vec<u8>,
    serial: Vec<u8>,
}

struct Specs {
    keyboard: *mut sys::aoahid_spec,
    mouse: *mut sys::aoahid_spec,
    touchscreen: *mut sys::aoahid_spec,
}

struct Session {
    locator: Locator,
    device: *mut sys::aoahid_device,
    keyboard: *mut sys::aoahid_node,
    mouse: *mut sys::aoahid_node,
    touchscreen: *mut sys::aoahid_node,
}

fn struct_size<T>() -> u32 {
    u32::try_from(std::mem::size_of::<T>()).expect("public option structures fit struct_size")
}

fn context_options() -> sys::aoahid_context_options {
    sys::aoahid_context_options {
        struct_size: struct_size::<sys::aoahid_context_options>(),
        reserved: 0,
        event_mode: sys::AOAHID_EVENT_CALLER_POLL,
        log_level: sys::AOAHID_LOG_DISABLED,
        log_sink: None,
        log_user: ptr::null_mut(),
    }
}

fn device_options(timeout_ms: u32) -> sys::aoahid_device_options {
    sys::aoahid_device_options {
        struct_size: struct_size::<sys::aoahid_device_options>(),
        reserved: 0,
        control_timeout_ms: timeout_ms,
        send_timeout_ms: timeout_ms,
        descriptor_fragment_bytes: 64, // Explicit policy; AOA fixes no fragment size.
        transfer_pool_slots: POOL_SLOTS,
        maximum_report_bytes: HOST_REPORT_POLICY_BYTES,
        close_drain_timeout_ms: CLOSE_DRAIN_TIMEOUT_MS,
        aoa_descriptor_wire_policy_bytes: DESCRIPTOR_POLICY_BYTES,
        linux_descriptor_policy_bytes: DESCRIPTOR_POLICY_BYTES,
        // These policies mirror Linux HID core limits
        // (include/linux/hid.h); check them against the target kernel.
        linux_hid_fields_per_report_policy: 256,
        linux_hid_global_stack_depth_policy: 4,
        linux_hid_usages_policy: 12288,
        linux_hid_report_data_bits_policy: 65528,
        linux_hid_report_size_bits_policy: 256,
        target_ep0_data_policy_bytes: DESCRIPTOR_POLICY_BYTES,
        host_control_buffer_policy_bytes: HOST_REPORT_POLICY_BYTES,
        interface_claim_policy: sys::AOAHID_INTERFACE_CLAIM_NONE,
        interface_number: -1, // No interface number accompanies the no-claim policy.
        validate_reports: 1,
    }
}

fn node_options() -> sys::aoahid_node_options {
    sys::aoahid_node_options {
        struct_size: struct_size::<sys::aoahid_node_options>(),
        reserved: 0,
        has_reserved_slots: 1,
        reserved_slots: RESERVED_SLOTS_PER_NODE,
    }
}

fn field(minimum: i32, maximum: i32, bits: u32) -> sys::aoahid_integer_field {
    sys::aoahid_integer_field {
        logical_minimum: minimum,
        logical_maximum: maximum,
        bit_width: bits,
        physical: sys::aoahid_physical_properties {
            enabled: 0,
            minimum: 0,
            maximum: 0,
            unit_exponent: 0,
            unit: 0,
        },
    }
}

fn report_id_absent() -> sys::aoahid_report_id_option {
    sys::aoahid_report_id_option {
        enabled: 0,
        value: 0,
        reserved8: [0; 3],
    }
}

fn create_specs() -> Result<Specs, sys::aoahid_result> {
    let keyboard_options = sys::aoahid_keyboard_options {
        struct_size: struct_size::<sys::aoahid_keyboard_options>(),
        reserved: 0,
        report_id: report_id_absent(),
        usage_minimum: KEYBOARD_A_USAGE,
        usage_maximum: KEYBOARD_APPLICATION_USAGE,
    };
    let mouse_options = sys::aoahid_mouse_options {
        struct_size: struct_size::<sys::aoahid_mouse_options>(),
        reserved: 0,
        report_id: report_id_absent(),
        button_count: POINTER_BUTTON_COUNT,
        x: field(RELATIVE_MINIMUM, RELATIVE_MAXIMUM, RELATIVE_BITS),
        y: field(RELATIVE_MINIMUM, RELATIVE_MAXIMUM, RELATIVE_BITS),
        enable_wheel: 1,
        wheel: field(RELATIVE_MINIMUM, RELATIVE_MAXIMUM, RELATIVE_BITS),
        enable_pan: 0,
        pan: field(0, 0, 0),
    };
    let touchscreen_options = sys::aoahid_touchscreen_options {
        struct_size: struct_size::<sys::aoahid_touchscreen_options>(),
        reserved: 0,
        report_id: report_id_absent(),
        maximum_contacts: 1,
        contacts_per_report: 1,
        contact_identifier: field(0, TOUCH_CONTACT_ID_MAXIMUM, TOUCH_CONTACT_ID_BITS),
        x: field(0, TOUCH_COORDINATE_MAXIMUM, TOUCH_COORDINATE_BITS),
        y: field(0, TOUCH_COORDINATE_MAXIMUM, TOUCH_COORDINATE_BITS),
        contact_count: field(0, 1, TOUCH_CONTACT_COUNT_BITS),
        enable_pressure: 0,
        pressure: field(0, 0, 0),
        enable_width: 0,
        width: field(0, 0, 0),
        enable_height: 0,
        height: field(0, 0, 0),
        enable_azimuth: 0,
        azimuth: field(0, 0, 0),
        enable_scan_time: 0,
        scan_time: field(0, 0, 0),
        scan_time_unit_100us: 0,
        enable_contact_count_maximum_feature_declaration: 0,
        enable_multi_packet_frames: 0,
    };
    let mut specs = Specs {
        keyboard: ptr::null_mut(),
        mouse: ptr::null_mut(),
        touchscreen: ptr::null_mut(),
    };
    let mut status =
        unsafe { sys::aoahid_spec_create_keyboard(&keyboard_options, &mut specs.keyboard) };
    if status == sys::AOAHID_OK {
        status = unsafe { sys::aoahid_spec_create_mouse(&mouse_options, &mut specs.mouse) };
    }
    if status == sys::AOAHID_OK {
        status =
            unsafe { sys::aoahid_spec_create_touchscreen(&touchscreen_options, &mut specs.touchscreen) };
    }
    if status == sys::AOAHID_OK {
        Ok(specs)
    } else {
        release_specs(&mut specs);
        Err(status)
    }
}

fn release_specs(specs: &mut Specs) {
    for spec in [specs.keyboard, specs.mouse, specs.touchscreen] {
        if !spec.is_null() {
            unsafe { sys::aoahid_spec_release(spec) };
        }
    }
    specs.keyboard = ptr::null_mut();
    specs.mouse = ptr::null_mut();
    specs.touchscreen = ptr::null_mut();
}

unsafe fn bytes_from(pointer: *const u8, length: usize) -> Vec<u8> {
    if pointer.is_null() || length == 0 {
        Vec::new()
    } else {
        slice::from_raw_parts(pointer, length).to_vec()
    }
}

unsafe fn c_bytes(pointer: *const core::ffi::c_char) -> Vec<u8> {
    if pointer.is_null() {
        Vec::new()
    } else {
        CStr::from_ptr(pointer).to_bytes().to_vec()
    }
}

unsafe fn locator_from(info: &sys::aoahid_device_info) -> Option<Locator> {
    let port_path = bytes_from(info.port_path, info.port_path_length);
    // This caller never falls back to ambiguous bus-only reconnection matching.
    if port_path.is_empty() {
        return None;
    }
    Some(Locator {
        bus_number: info.bus_number,
        port_path,
        serial: c_bytes(info.serial),
    })
}

unsafe fn matches(locator: &Locator, info: &sys::aoahid_device_info) -> bool {
    locator_from(info).is_some_and(|candidate| {
        candidate.bus_number == locator.bus_number
            && candidate.port_path == locator.port_path
            && (locator.serial.is_empty() || candidate.serial == locator.serial)
    })
}

unsafe fn text(pointer: *const core::ffi::c_char) -> String {
    if pointer.is_null() {
        String::new()
    } else {
        CStr::from_ptr(pointer).to_string_lossy().into_owned()
    }
}

unsafe fn print_info(info: &sys::aoahid_device_info) {
    let ports = bytes_from(info.port_path, info.port_path_length)
        .iter()
        .map(u8::to_string)
        .collect::<Vec<_>>()
        .join(".");
    println!(
        "bus={} address={} port={} vid:pid={:04x}:{:04x} aoa={} product={:?} serial={:?}",
        info.bus_number,
        info.device_address,
        ports,
        info.vendor_id,
        info.product_id,
        info.protocol_version,
        text(info.product),
        text(info.serial)
    );
}

fn report_error(status: sys::aoahid_result, operation: &str) {
    unsafe {
        let detail = sys::aoahid_last_error();
        // aoahid_result_name is another public call and clears TLS diagnostics;
        // own both borrowed strings before invoking it.
        let copied_detail =
            (!detail.is_null()).then(|| (text((*detail).field), text((*detail).reason)));
        let result_name = text(sys::aoahid_result_name(status));
        if detail.is_null() {
            eprintln!("{operation}: {result_name}");
        } else {
            let (field, reason) = copied_detail.expect("nonnull diagnostic was copied");
            eprintln!("{operation}: {result_name} ({}: {})", field, reason);
        }
    }
}

fn last_error_code_or_internal() -> sys::aoahid_result {
    let detail = unsafe { sys::aoahid_last_error() };
    if detail.is_null() {
        return sys::AOAHID_ERR_INTERNAL;
    }
    let status = unsafe { (*detail).code };
    if status == sys::AOAHID_OK {
        sys::AOAHID_ERR_INTERNAL
    } else {
        status
    }
}

fn discovery_count_checked(
    discovery: *const sys::aoahid_discovery,
    status: &mut sys::aoahid_result,
) -> usize {
    let count = unsafe { sys::aoahid_discovery_count(discovery) };
    if count != 0 {
        return count;
    }
    let detail = unsafe { sys::aoahid_last_error() };
    if detail.is_null() {
        *status = sys::AOAHID_ERR_INTERNAL;
    } else {
        let count_status = unsafe { (*detail).code };
        if count_status != sys::AOAHID_OK {
            *status = count_status;
        }
    }
    0
}

unsafe fn open_session(
    context: *mut sys::aoahid_context,
    info: &sys::aoahid_device_info,
    locator: Locator,
    specs: &Specs,
    timeout_ms: u32,
) -> Result<Session, sys::aoahid_result> {
    // Open sends no AOA request, so the version check is the caller's.
    if info.protocol_version < 2 {
        eprintln!(
            "device open: AOA {} has no HID; version 2 is required",
            info.protocol_version
        );
        return Err(sys::AOAHID_ERR_NOT_AOA);
    }
    let options = device_options(timeout_ms);
    let mut session = Session {
        locator,
        device: ptr::null_mut(),
        keyboard: ptr::null_mut(),
        mouse: ptr::null_mut(),
        touchscreen: ptr::null_mut(),
    };
    let mut status = sys::aoahid_device_open(context, info, &options, &mut session.device);
    if status != sys::AOAHID_OK {
        report_error(status, "device open");
        return Err(status);
    }
    let nodes = node_options();
    status = sys::aoahid_node_open(
        session.device,
        specs.keyboard,
        &nodes,
        &mut session.keyboard,
    );
    if status == sys::AOAHID_OK {
        status = sys::aoahid_node_open(session.device, specs.mouse, &nodes, &mut session.mouse);
    }
    if status == sys::AOAHID_OK {
        status = sys::aoahid_node_open(
            session.device,
            specs.touchscreen,
            &nodes,
            &mut session.touchscreen,
        );
    }
    if status == sys::AOAHID_OK {
        Ok(session)
    } else {
        // Own and print the failing call's TLS strings before Device close clears them.
        report_error(status, "profile registration");
        close_device(&mut session);
        Err(status)
    }
}

fn close_device(session: &mut Session) -> sys::aoahid_result {
    if session.device.is_null() {
        return sys::AOAHID_OK;
    }
    let status = unsafe { sys::aoahid_device_close(session.device) };
    // The first Device close consumes it and every Node handle for every result.
    session.device = ptr::null_mut();
    session.keyboard = ptr::null_mut();
    session.mouse = ptr::null_mut();
    session.touchscreen = ptr::null_mut();
    if !matches!(
        status,
        sys::AOAHID_OK | sys::AOAHID_CLOSE_PENDING | sys::AOAHID_ERR_NO_DEVICE
    ) {
        report_error(status, "device close");
    }
    status
}

fn close_touchscreen(
    context: *mut sys::aoahid_context,
    session: &mut Session,
    poll_ms: u32,
) -> sys::aoahid_result {
    while !session.touchscreen.is_null() {
        let status = unsafe { sys::aoahid_node_close(session.touchscreen) };
        if status == sys::AOAHID_OK {
            session.touchscreen = ptr::null_mut();
            return status;
        }
        if status != sys::AOAHID_CLOSE_PENDING {
            return status;
        }
        // Pending Node close retains its handle; polling and retry are required.
        let poll = unsafe { sys::aoahid_context_poll(context, poll_ms) };
        if poll != sys::AOAHID_OK {
            return poll;
        }
    }
    sys::AOAHID_OK
}

fn destroy_context(context: *mut sys::aoahid_context, poll_ms: u32) -> sys::aoahid_result {
    loop {
        let status = unsafe { sys::aoahid_context_destroy(context) };
        if status != sys::AOAHID_CLOSE_PENDING {
            return status; // Every non-pending valid destroy result consumes Context.
        }
        let poll = unsafe { sys::aoahid_context_poll(context, poll_ms) };
        if poll != sys::AOAHID_OK {
            report_error(poll, "context poll during shutdown");
        }
    }
}

fn first_failure(statuses: impl IntoIterator<Item = sys::aoahid_result>) -> sys::aoahid_result {
    statuses
        .into_iter()
        .find(|status| *status != sys::AOAHID_OK)
        .unwrap_or(sys::AOAHID_OK)
}

fn update_then_submit(
    sessions: &mut [Session],
    timeout_ms: u32,
) -> (Vec<Locator>, sys::aoahid_result) {
    let contact = sys::aoahid_touch_contact {
        contact_id: TOUCH_CONTACT_ID,
        x: EXAMPLE_TOUCH_X,
        y: EXAMPLE_TOUCH_Y,
        pressure: 0,
        width: 0,
        height: 0,
        azimuth: 0,
    };
    let mut disappeared = Vec::new();
    let mut result = sys::AOAHID_OK;
    let mut ready = Vec::new();
    // Prepare every node first. USB submission is a second pass so one device
    // does not delay the next device's preparation.
    for (index, session) in sessions.iter_mut().enumerate() {
        if session.device.is_null() {
            continue;
        }
        let status = first_failure([
            unsafe { sys::aoahid_kbd(session.keyboard, KEYBOARD_A_USAGE, 1) },
            unsafe { sys::aoahid_mouse_move(session.mouse, EXAMPLE_MOUSE_DX, EXAMPLE_MOUSE_DY) },
            unsafe {
                sys::aoahid_touchscreen_contact(
                    session.touchscreen,
                    contact.contact_id,
                    1,
                    contact.x,
                    contact.y,
                    std::ptr::null(),
                )
            },
        ]);
        if status == sys::AOAHID_ERR_NO_DEVICE {
            disappeared.push(session.locator.clone());
            close_device(session);
        } else if status == sys::AOAHID_OK {
            ready.push(index);
        } else {
            report_error(status, "profile update");
            if result == sys::AOAHID_OK {
                result = status;
            }
        }
    }
    for index in ready {
        let session = &mut sessions[index];
        let status = first_failure([
            unsafe { sys::aoahid_node_submit_blocking(session.keyboard, timeout_ms) },
            unsafe { sys::aoahid_node_submit_blocking(session.mouse, timeout_ms) },
            unsafe { sys::aoahid_node_submit_blocking(session.touchscreen, timeout_ms) },
        ]);
        if status == sys::AOAHID_ERR_NO_DEVICE {
            disappeared.push(session.locator.clone());
            close_device(session);
        } else if status != sys::AOAHID_OK {
            report_error(status, "profile submit");
            if result == sys::AOAHID_OK {
                result = status;
            }
        }
    }
    (disappeared, result)
}

fn reopen(
    context: *mut sys::aoahid_context,
    sessions: &mut Vec<Session>,
    pending: &mut Vec<Locator>,
    specs: &Specs,
    timeout_ms: u32,
) -> sys::aoahid_result {
    if pending.is_empty() {
        return sys::AOAHID_OK;
    }
    let mut discovery = ptr::null_mut();
    let status = unsafe { sys::aoahid_discover(context, timeout_ms, &mut discovery) };
    if status != sys::AOAHID_OK {
        return status;
    }
    let mut result = sys::AOAHID_OK;
    let count = discovery_count_checked(discovery, &mut result);
    for index in 0..count {
        let pointer = unsafe { sys::aoahid_discovery_get(discovery, index) };
        if pointer.is_null() {
            result = last_error_code_or_internal();
            break;
        }
        let found = pending
            .iter()
            .position(|locator| unsafe { matches(locator, &*pointer) });
        let Some(found) = found else { continue };
        let locator = pending[found].clone();
        match unsafe { open_session(context, &*pointer, locator, specs, timeout_ms) } {
            Ok(session) => {
                sessions.push(session);
                pending.remove(found);
            }
            Err(error) => {
                if result == sys::AOAHID_OK {
                    result = error;
                }
            }
        }
    }
    unsafe { sys::aoahid_discovery_destroy(discovery) };
    if !pending.is_empty() && result == sys::AOAHID_OK {
        sys::AOAHID_ERR_NO_DEVICE
    } else {
        result
    }
}

fn continue_without_touch(
    context: *mut sys::aoahid_context,
    sessions: &mut [Session],
    timeout_ms: u32,
) -> sys::aoahid_result {
    let mut result = sys::AOAHID_OK;
    for session in sessions.iter_mut().filter(|value| !value.device.is_null()) {
        let status = close_touchscreen(context, session, timeout_ms);
        if status != sys::AOAHID_OK {
            result = status;
        }
    }
    let mut ready = Vec::new();
    for (index, session) in sessions.iter_mut().enumerate() {
        if session.device.is_null() {
            continue;
        }
        let status = first_failure([
            unsafe { sys::aoahid_kbd(session.keyboard, KEYBOARD_A_USAGE, 0) },
            // This caller selects pointer button 1 and a pressed level of one.
            unsafe { sys::aoahid_mouse_button(session.mouse, 1, 1) },
        ]);
        if status == sys::AOAHID_OK {
            ready.push(index);
        } else if status == sys::AOAHID_ERR_NO_DEVICE {
            close_device(session);
        } else {
            result = status;
        }
    }
    for index in ready {
        let session = &sessions[index];
        let status = first_failure([
            unsafe { sys::aoahid_node_submit_blocking(session.keyboard, timeout_ms) },
            unsafe { sys::aoahid_node_submit_blocking(session.mouse, timeout_ms) },
        ]);
        if status != sys::AOAHID_OK {
            result = status;
        }
    }
    result
}

fn drive_context(
    context: *mut sys::aoahid_context,
    selected: Option<&Locator>,
    timeout_ms: u32,
) -> sys::aoahid_result {
    let mut specs = match create_specs() {
        Ok(value) => value,
        Err(error) => return error,
    };
    let mut sessions = Vec::new();
    let mut result = sys::AOAHID_OK;
    let mut discovery = ptr::null_mut();
    let discover = unsafe { sys::aoahid_discover(context, timeout_ms, &mut discovery) };
    if discover != sys::AOAHID_OK {
        result = discover;
    } else {
        let count = discovery_count_checked(discovery, &mut result);
        let mut found_selected = false;
        for index in 0..count {
            let pointer = unsafe { sys::aoahid_discovery_get(discovery, index) };
            if pointer.is_null() {
                result = last_error_code_or_internal();
                break;
            }
            let locator = unsafe { locator_from(&*pointer) };
            if selected.is_none() {
                unsafe { print_info(&*pointer) };
            }
            let Some(locator) = locator else {
                if selected.is_none() && result == sys::AOAHID_OK {
                    eprintln!("stable device locator: caller policy requires a physical port path");
                    result = sys::AOAHID_ERR_UNSUPPORTED;
                }
                continue;
            };
            if selected.is_some_and(|value| value != &locator) {
                continue;
            }
            if selected.is_some() {
                found_selected = true;
            }
            match unsafe { open_session(context, &*pointer, locator, &specs, timeout_ms) } {
                Ok(session) => sessions.push(session),
                Err(error) => {
                    if result == sys::AOAHID_OK {
                        result = error;
                    }
                }
            }
            if selected.is_some() {
                break;
            }
        }
        unsafe { sys::aoahid_discovery_destroy(discovery) };
        if selected.is_some() && !found_selected && result == sys::AOAHID_OK {
            result = sys::AOAHID_ERR_NO_DEVICE;
        }

        let (mut disappeared, drive_status) = update_then_submit(&mut sessions, timeout_ms);
        if result == sys::AOAHID_OK {
            result = drive_status;
        }
        // Only missing sessions are reopened; live siblings remain open.
        let reopened = reopen(context, &mut sessions, &mut disappeared, &specs, timeout_ms);
        if result == sys::AOAHID_OK {
            result = reopened;
        }
        let continuation = continue_without_touch(context, &mut sessions, timeout_ms);
        if result == sys::AOAHID_OK {
            result = continuation;
        }
    }
    for session in &mut sessions {
        close_device(session);
    }
    release_specs(&mut specs);
    result
}

fn create_context() -> Result<*mut sys::aoahid_context, sys::aoahid_result> {
    let options = context_options();
    let mut context = ptr::null_mut();
    let status = unsafe { sys::aoahid_context_create(&options, &mut context) };
    if status == sys::AOAHID_OK {
        Ok(context)
    } else {
        Err(status)
    }
}

fn run_shared(timeout_ms: u32) -> sys::aoahid_result {
    let context = match create_context() {
        Ok(value) => value,
        Err(error) => return error,
    };
    let mut result = drive_context(context, None, timeout_ms);
    let close = destroy_context(context, timeout_ms);
    if result == sys::AOAHID_OK {
        result = close;
    }
    result
}

fn enumerate_locators(timeout_ms: u32) -> Result<Vec<Locator>, sys::aoahid_result> {
    let context = create_context()?;
    let mut locators = Vec::new();
    let mut discovery = ptr::null_mut();
    let mut result = unsafe { sys::aoahid_discover(context, timeout_ms, &mut discovery) };
    if result == sys::AOAHID_OK {
        let count = discovery_count_checked(discovery, &mut result);
        for index in 0..count {
            let pointer = unsafe { sys::aoahid_discovery_get(discovery, index) };
            if pointer.is_null() {
                result = last_error_code_or_internal();
                break;
            }
            unsafe {
                print_info(&*pointer);
                if let Some(locator) = locator_from(&*pointer) {
                    locators.push(locator);
                } else {
                    eprintln!("stable device locator: caller policy requires a physical port path");
                    result = sys::AOAHID_ERR_UNSUPPORTED;
                    break;
                }
            }
        }
        unsafe { sys::aoahid_discovery_destroy(discovery) };
    }
    let close = destroy_context(context, timeout_ms);
    if result != sys::AOAHID_OK {
        Err(result)
    } else if close != sys::AOAHID_OK {
        Err(close)
    } else {
        Ok(locators)
    }
}

fn run_worker(locator: &Locator, timeout_ms: u32) -> sys::aoahid_result {
    let context = match create_context() {
        Ok(value) => value,
        Err(error) => return error,
    };
    // Reconnection and all event handling remain inside this worker/context;
    // sibling workers therefore keep independent synchronization domains.
    let mut result = drive_context(context, Some(locator), timeout_ms);
    let close = destroy_context(context, timeout_ms);
    if result == sys::AOAHID_OK {
        result = close;
    }
    result
}

fn run_threaded(timeout_ms: u32) -> sys::aoahid_result {
    let locators = match enumerate_locators(timeout_ms) {
        Ok(value) => value,
        Err(error) => return error,
    };
    let mut results = Vec::new();
    // The worker count comes exactly from discovery; no device-count ceiling exists.
    thread::scope(|scope| {
        let handles = locators
            .iter()
            .map(|locator| scope.spawn(move || run_worker(locator, timeout_ms)))
            .collect::<Vec<_>>();
        for handle in handles {
            results.push(handle.join().unwrap_or(sys::AOAHID_ERR_INTERNAL));
        }
    });
    first_failure(results)
}

fn main() {
    let mut arguments = std::env::args().skip(1);
    let timeout_ms = arguments
        .next()
        .and_then(|value| value.parse::<u32>().ok())
        .filter(|value| *value != 0);
    let mode = arguments.next();
    if arguments.next().is_some()
        || timeout_ms.is_none()
        || !matches!(mode.as_deref(), Some("shared" | "threaded"))
    {
        eprintln!("usage: aoahid-multi-profile-example CONTROL_TIMEOUT_MS shared|threaded");
        std::process::exit(2);
    }
    let status = if mode.as_deref() == Some("shared") {
        run_shared(timeout_ms.unwrap())
    } else {
        run_threaded(timeout_ms.unwrap())
    };
    std::process::exit(status);
}
