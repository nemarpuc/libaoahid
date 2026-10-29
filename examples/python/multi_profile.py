#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libaoahid contributors
"""Exercise explicit multi-device lifecycle policies through the ctypes ABI.

This caller example owns discovery identity, threading, retry, and failure
policy.  The binding only mirrors ``aoahid.h`` and provides no option values.
``docs/EXAMPLES.md`` explains the selected profile values.
"""

from __future__ import annotations

import ctypes as c
from dataclasses import dataclass
import sys
import threading
from typing import Iterable, Optional

from aoahid.native import *  # noqa: F403 - the example deliberately mirrors the C ABI.


# Every value in this block is this caller's reviewed example policy or input.
# None comes from libaoahid.  See docs/EXAMPLES.md, "Example values".
DESCRIPTOR_POLICY_BYTES = 4096
HOST_REPORT_POLICY_BYTES = 4088
POOL_SLOTS = 8
RESERVED_SLOTS_PER_NODE = 1
CLOSE_DRAIN_TIMEOUT_MS = 1000
KEYBOARD_A_USAGE = 0x04  # HUT 1.7 section 10.
KEYBOARD_APPLICATION_USAGE = 0x65  # HUT 1.7 section 10.
POINTER_BUTTON_COUNT = 3
RELATIVE_MINIMUM = -127
RELATIVE_MAXIMUM = 127
RELATIVE_BITS = 8
TOUCH_CONTACT_ID = 1
TOUCH_CONTACT_ID_MAXIMUM = 15
TOUCH_CONTACT_ID_BITS = 4
TOUCH_COORDINATE_MAXIMUM = 32767
TOUCH_COORDINATE_BITS = 16
TOUCH_CONTACT_COUNT_BITS = 1
EXAMPLE_TOUCH_X = 1000
EXAMPLE_TOUCH_Y = 2000
EXAMPLE_MOUSE_DX = 30
EXAMPLE_MOUSE_DY = -20


@dataclass(frozen=True)
class Locator:
    """Own the caller-selected physical identity after discovery is destroyed."""

    bus_number: int
    port_path: bytes
    serial: bytes


@dataclass
class Specs:
    keyboard: SpecP  # noqa: F405
    mouse: SpecP  # noqa: F405
    touchscreen: SpecP  # noqa: F405


@dataclass
class Session:
    locator: Locator
    device: DeviceP  # noqa: F405
    keyboard: NodeP  # noqa: F405
    mouse: NodeP  # noqa: F405
    touchscreen: NodeP  # noqa: F405


class AoaHidFailure(RuntimeError):
    """Preserve the exact native result while unwinding caller-owned handles."""

    def __init__(self, status: int, message: str) -> None:
        super().__init__(message)
        self.status = status


def status_text(library: c.CDLL, status: int) -> str:
    value = library.aoahid_result_name(status)
    return value.decode(errors="replace") if value else f"result {status}"


def report_error(library: c.CDLL, status: int, operation: str) -> None:
    detail_pointer = library.aoahid_last_error()
    if not detail_pointer:
        print(f"{operation}: {status_text(library, status)}", file=sys.stderr)
        return
    detail = detail_pointer.contents
    # Decode both borrowed strings before status_text calls result_name, because
    # every public call except last_error invalidates this TLS diagnostic record.
    field = detail.field.decode(errors="replace") if detail.field else "no field"
    reason = detail.reason.decode(errors="replace") if detail.reason else "no detail"
    print(f"{operation}: {status_text(library, status)} ({field}: {reason})", file=sys.stderr)


def context_options() -> ContextOptions:  # noqa: F405
    options = ContextOptions()  # noqa: F405
    options.struct_size = c.sizeof(ContextOptions)  # noqa: F405
    options.reserved = 0
    options.event_mode = AOAHID_EVENT_CALLER_POLL  # noqa: F405
    options.log_level = AOAHID_LOG_DISABLED  # noqa: F405
    options.log_sink = LogSink()  # noqa: F405
    options.log_user = None
    return options


def device_options() -> DeviceOptions:  # noqa: F405
    options = DeviceOptions()  # noqa: F405
    options.struct_size = c.sizeof(DeviceOptions)  # noqa: F405
    options.reserved = 0
    options.descriptor_fragment_bytes = 64  # Explicit policy; AOA fixes no fragment size.
    options.transfer_pool_slots = POOL_SLOTS
    options.maximum_report_bytes = HOST_REPORT_POLICY_BYTES
    options.validate_reports = 1
    options.aoa_descriptor_wire_policy_bytes = DESCRIPTOR_POLICY_BYTES
    # The Linux HID parser limits of include/linux/hid.h (docs/LIMITS.md); check them against the
    # target kernel.
    options.linux_hid.descriptor_bytes = DESCRIPTOR_POLICY_BYTES
    options.linux_hid.fields_per_report = 256
    options.linux_hid.global_stack_depth = 4
    options.linux_hid.report_size_bits = 256
    options.linux_hid.usages = 12288
    options.linux_hid.report_data_bits = 65528
    options.target_ep0_data_policy_bytes = DESCRIPTOR_POLICY_BYTES
    options.host_control_buffer_policy_bytes = HOST_REPORT_POLICY_BYTES
    options.interface_claim_policy = AOAHID_INTERFACE_CLAIM_NONE  # noqa: F405
    options.interface_number = -1  # No interface number accompanies the no-claim policy.
    return options


def node_options() -> NodeOptions:  # noqa: F405
    options = NodeOptions()  # noqa: F405
    options.struct_size = c.sizeof(NodeOptions)  # noqa: F405
    options.reserved = 0
    options.has_reserved_slots = 1
    options.reserved_slots = RESERVED_SLOTS_PER_NODE
    return options


def integer_field(minimum: int, maximum: int, bits: int) -> IntegerField:  # noqa: F405
    return IntegerField(minimum, maximum, bits, PhysicalProperties(0, 0, 0, 0, 0))  # noqa: F405


def create_specs(library: c.CDLL) -> Specs:
    report_id = ReportId(0, 0, (c.c_uint8 * 3)(0, 0, 0))  # noqa: F405
    keyboard_options = KeyboardOptions()  # noqa: F405
    keyboard_options.struct_size = c.sizeof(KeyboardOptions)  # noqa: F405
    keyboard_options.reserved = 0
    keyboard_options.report_id = report_id
    keyboard_options.usage_minimum = KEYBOARD_A_USAGE
    keyboard_options.usage_maximum = KEYBOARD_APPLICATION_USAGE

    mouse_options = MouseOptions()  # noqa: F405
    mouse_options.struct_size = c.sizeof(MouseOptions)  # noqa: F405
    mouse_options.reserved = 0
    mouse_options.report_id = report_id
    mouse_options.button_count = POINTER_BUTTON_COUNT
    mouse_options.x = integer_field(RELATIVE_MINIMUM, RELATIVE_MAXIMUM, RELATIVE_BITS)
    mouse_options.y = integer_field(RELATIVE_MINIMUM, RELATIVE_MAXIMUM, RELATIVE_BITS)
    mouse_options.enable_wheel = 1
    mouse_options.wheel = integer_field(RELATIVE_MINIMUM, RELATIVE_MAXIMUM, RELATIVE_BITS)
    mouse_options.enable_pan = 0
    mouse_options.pan = integer_field(0, 0, 0)

    touchscreen_options = TouchscreenOptions()  # noqa: F405
    touchscreen_options.struct_size = c.sizeof(TouchscreenOptions)  # noqa: F405
    touchscreen_options.reserved = 0
    touchscreen_options.report_id = report_id
    touchscreen_options.maximum_contacts = 1
    touchscreen_options.contacts_per_report = 1
    touchscreen_options.contact_id = integer_field(
        0, TOUCH_CONTACT_ID_MAXIMUM, TOUCH_CONTACT_ID_BITS
    )
    touchscreen_options.x = integer_field(0, TOUCH_COORDINATE_MAXIMUM, TOUCH_COORDINATE_BITS)
    touchscreen_options.y = integer_field(0, TOUCH_COORDINATE_MAXIMUM, TOUCH_COORDINATE_BITS)
    touchscreen_options.contact_count = integer_field(0, 1, TOUCH_CONTACT_COUNT_BITS)
    touchscreen_options.enable_pressure = 0
    touchscreen_options.pressure = integer_field(0, 0, 0)
    touchscreen_options.enable_width = 0
    touchscreen_options.width = integer_field(0, 0, 0)
    touchscreen_options.enable_height = 0
    touchscreen_options.height = integer_field(0, 0, 0)
    touchscreen_options.enable_azimuth = 0
    touchscreen_options.azimuth = integer_field(0, 0, 0)
    touchscreen_options.enable_scan_time = 0
    touchscreen_options.scan_time = integer_field(0, 0, 0)
    touchscreen_options.scan_time_unit_100us = 0
    touchscreen_options.enable_contact_count_maximum_feature_declaration = 0
    touchscreen_options.enable_multi_packet_frames = 0

    keyboard = SpecP()  # noqa: F405
    mouse = SpecP()  # noqa: F405
    touchscreen = SpecP()  # noqa: F405
    creations = (
        (library.aoahid_spec_create_keyboard(c.byref(keyboard_options), c.byref(keyboard)), "keyboard spec"),
        (library.aoahid_spec_create_mouse(c.byref(mouse_options), c.byref(mouse)), "mouse spec"),
        (library.aoahid_spec_create_touchscreen(c.byref(touchscreen_options), c.byref(touchscreen)), "touchscreen spec"),
    )
    for status, operation in creations:
        if status != AOAHID_OK:  # noqa: F405
            for spec in (keyboard, mouse, touchscreen):
                if spec:
                    library.aoahid_spec_release(spec)
            raise AoaHidFailure(status, f"{operation}: {status_text(library, status)}")
    return Specs(keyboard, mouse, touchscreen)


def release_specs(library: c.CDLL, specs: Specs) -> None:
    library.aoahid_spec_release(specs.keyboard)
    library.aoahid_spec_release(specs.mouse)
    library.aoahid_spec_release(specs.touchscreen)


def reported_port_path(info: DeviceInfo) -> bytes:  # noqa: F405
    return c.string_at(info.port_path, info.port_path_length) if info.port_path else b""


def locator_from(info: DeviceInfo) -> Optional[Locator]:  # noqa: F405
    port_path = reported_port_path(info)
    # This caller never falls back to ambiguous bus-only reconnection matching.
    if not port_path:
        return None
    serial = c.string_at(info.serial) if info.serial else b""
    return Locator(int(info.bus_number), port_path, serial)


def locator_matches(locator: Locator, info: DeviceInfo) -> bool:  # noqa: F405
    candidate = locator_from(info)
    # This example selects physical bus/port identity and, when reported, serial.
    return (
        candidate is not None
        and candidate.bus_number == locator.bus_number
        and candidate.port_path == locator.port_path
        and (not locator.serial or candidate.serial == locator.serial)
    )


def print_info(info: DeviceInfo) -> None:  # noqa: F405
    product = c.string_at(info.product).decode(errors="replace") if info.product else ""
    serial = c.string_at(info.serial).decode(errors="replace") if info.serial else ""
    ports = ".".join(str(value) for value in reported_port_path(info))
    print(
        f"bus={info.bus_number} address={info.device_address} port={ports} "
        f"vid:pid={info.vendor_id:04x}:{info.product_id:04x} aoa={info.protocol_version} "
        f"product={product!r} serial={serial!r}"
    )


def open_session(
    library: c.CDLL,
    context: ContextP,  # noqa: F405
    info: DeviceInfo,  # noqa: F405
    locator: Locator,
    specs: Specs,
    control_timeout_ms: int,
) -> tuple[Optional[Session], int]:
    # Open sends no AOA request, so the version check is the caller's.
    if info.protocol_version < 2:
        print(f"device open: AOA {info.protocol_version} has no HID; version 2 is required")
        return None, AOAHID_ERR_NOT_AOA  # noqa: F405
    device = DeviceP()  # noqa: F405
    options = device_options()
    status = library.aoahid_device_open(context, c.byref(info), c.byref(options), c.byref(device))
    if status != AOAHID_OK:  # noqa: F405
        report_error(library, status, "device open")
        return None, int(status)
    nodes = [NodeP(), NodeP(), NodeP()]  # noqa: F405
    registrations = (specs.keyboard, specs.mouse, specs.touchscreen)
    selected_node_options = node_options()
    for index, spec in enumerate(registrations):
        status = library.aoahid_node_open(
            device,
            spec,
            c.byref(selected_node_options),
            control_timeout_ms,
            c.byref(nodes[index]),
        )
        if status != AOAHID_OK:  # noqa: F405
            report_error(library, status, "profile registration")
            library.aoahid_device_close(
                device, control_timeout_ms, CLOSE_DRAIN_TIMEOUT_MS
            )  # Every result of a valid close consumes the Device and its Nodes.
            return None, int(status)
    return Session(locator, device, nodes[0], nodes[1], nodes[2]), AOAHID_OK  # noqa: F405


def close_device(library: c.CDLL, session: Session, control_timeout_ms: int) -> int:
    if not session.device:
        return AOAHID_OK  # noqa: F405
    status = library.aoahid_device_close(session.device, control_timeout_ms, CLOSE_DRAIN_TIMEOUT_MS)
    # Every result of a valid close consumes the Device and its Nodes.
    session.device = DeviceP()  # noqa: F405
    session.keyboard = NodeP()  # noqa: F405
    session.mouse = NodeP()  # noqa: F405
    session.touchscreen = NodeP()  # noqa: F405
    if status not in (AOAHID_OK, AOAHID_ERR_NO_DEVICE):  # noqa: F405
        report_error(library, status, "device close")
    return status


def close_touchscreen(library: c.CDLL, context: ContextP, session: Session, poll_ms: int) -> int:  # noqa: F405
    while session.touchscreen:
        status = library.aoahid_node_close(session.touchscreen, poll_ms, CLOSE_DRAIN_TIMEOUT_MS)
        if status != AOAHID_CLOSE_PENDING:  # noqa: F405
            # Any other result consumed the Node; an error says what failed on the way out.
            session.touchscreen = NodeP()  # noqa: F405
            if status != AOAHID_OK:  # noqa: F405
                report_error(library, status, "touchscreen close")
            return status
        # The Node is still ours: let its report finish, then close it again.
        poll = library.aoahid_context_poll(context, poll_ms)
        if poll != AOAHID_OK:  # noqa: F405
            report_error(library, poll, "context poll during touchscreen close")
            return poll
    return AOAHID_OK  # noqa: F405


def destroy_context(library: c.CDLL, context: ContextP, poll_ms: int) -> int:  # noqa: F405
    while context:
        status = library.aoahid_context_destroy(context, poll_ms, CLOSE_DRAIN_TIMEOUT_MS)
        if status != AOAHID_CLOSE_PENDING:  # noqa: F405
            return status  # Every non-pending result consumes a valid Context.
        poll = library.aoahid_context_poll(context, poll_ms)
        if poll != AOAHID_OK:  # noqa: F405
            report_error(library, poll, "context poll during shutdown")
    return AOAHID_OK  # noqa: F405


def discover(library: c.CDLL, context: ContextP, timeout_ms: int) -> DiscoveryP:  # noqa: F405
    result = DiscoveryP()  # noqa: F405
    status = library.aoahid_discover(context, timeout_ms, c.byref(result))
    if status != AOAHID_OK:  # noqa: F405
        raise AoaHidFailure(status, f"discovery: {status_text(library, status)}")
    return result


def discovery_info(library: c.CDLL, discovery: DiscoveryP, index: int) -> DeviceInfo:  # noqa: F405
    pointer = library.aoahid_discovery_get(discovery, index)
    if pointer:
        return pointer.contents
    detail_pointer = library.aoahid_last_error()
    status = AOAHID_ERR_INTERNAL  # noqa: F405
    field = "discovery.entry"
    reason = "the native API returned a null entry without a diagnostic"
    if detail_pointer:
        detail = detail_pointer.contents
        status = detail.code if detail.code != AOAHID_OK else status  # noqa: F405
        field = detail.field.decode(errors="replace") if detail.field else field
        reason = detail.reason.decode(errors="replace") if detail.reason else reason
    raise AoaHidFailure(status, f"discovery entry {index}: {field}: {reason}")


def discovery_count(library: c.CDLL, discovery: DiscoveryP) -> int:  # noqa: F405
    count = int(library.aoahid_discovery_count(discovery))
    if count != 0:
        return count
    detail_pointer = library.aoahid_last_error()
    if not detail_pointer or detail_pointer.contents.code == AOAHID_OK:  # noqa: F405
        return 0
    detail = detail_pointer.contents
    # Own both borrowed strings before the caller destroys the snapshot.
    field = detail.field.decode(errors="replace") if detail.field else "discovery.count"
    reason = detail.reason.decode(errors="replace") if detail.reason else "count failed"
    raise AoaHidFailure(detail.code, f"discovery count: {field}: {reason}")


def first_failure(statuses: Iterable[int]) -> int:
    return next((value for value in statuses if value != AOAHID_OK), AOAHID_OK)  # noqa: F405


def update_then_submit(
    library: c.CDLL, sessions: list[Session], timeout_ms: int
) -> tuple[list[Locator], int]:
    disappeared: list[Locator] = []
    result = AOAHID_OK  # noqa: F405
    # Update every device first.  Submitting in a second pass keeps one USB
    # operation out of the next device's state preparation.
    ready: list[Session] = []
    for session in sessions:
        if not session.device:
            continue
        status = first_failure(
            (
                library.aoahid_keyboard_key(session.keyboard, KEYBOARD_A_USAGE, 1),
                library.aoahid_mouse_move(session.mouse, EXAMPLE_MOUSE_DX, EXAMPLE_MOUSE_DY),
                library.aoahid_touchscreen_contact(
                    session.touchscreen, TOUCH_CONTACT_ID, 1, EXAMPLE_TOUCH_X, EXAMPLE_TOUCH_Y, None
                ),
            )
        )
        if status == AOAHID_ERR_NO_DEVICE:  # noqa: F405
            disappeared.append(session.locator)
            close_device(library, session, timeout_ms)
        elif status != AOAHID_OK:  # noqa: F405
            report_error(library, status, "profile update")
            if result == AOAHID_OK:  # noqa: F405
                result = status
        else:
            ready.append(session)
    for session in ready:
        status = first_failure(
            library.aoahid_node_submit_blocking(node, timeout_ms)
            for node in (session.keyboard, session.mouse, session.touchscreen)
        )
        if status == AOAHID_ERR_NO_DEVICE:  # noqa: F405
            disappeared.append(session.locator)
            close_device(library, session, timeout_ms)
        elif status != AOAHID_OK:  # noqa: F405
            report_error(library, status, "profile submit")
            if result == AOAHID_OK:  # noqa: F405
                result = status
    return disappeared, int(result)


def reopen(
    library: c.CDLL,
    context: ContextP,  # noqa: F405
    sessions: list[Session],
    locators: Iterable[Locator],
    specs: Specs,
    timeout_ms: int,
) -> int:
    pending = list(locators)
    if not pending:
        return AOAHID_OK  # noqa: F405
    result = AOAHID_OK  # noqa: F405
    current = discover(library, context, timeout_ms)
    try:
        for index in range(discovery_count(library, current)):
            info = discovery_info(library, current, index)
            match = next((locator for locator in pending if locator_matches(locator, info)), None)
            if match is None:
                continue
            reopened, open_status = open_session(
                library, context, info, match, specs, timeout_ms
            )
            if reopened is not None:
                sessions.append(reopened)
                pending.remove(match)
            elif result == AOAHID_OK:  # noqa: F405
                result = open_status
    finally:
        library.aoahid_discovery_destroy(current)
    if pending and result == AOAHID_OK:  # noqa: F405
        result = AOAHID_ERR_NO_DEVICE  # noqa: F405
    return int(result)


def continue_without_touch(library: c.CDLL, context: ContextP, sessions: list[Session], timeout_ms: int) -> int:  # noqa: F405
    result = AOAHID_OK  # noqa: F405
    for session in sessions:
        if not session.device:
            continue
        close = close_touchscreen(library, context, session, timeout_ms)
        if close != AOAHID_OK:  # noqa: F405
            result = close
    ready: list[Session] = []
    for session in sessions:
        if not session.device:
            continue
        status = first_failure(
            (
                library.aoahid_keyboard_key(session.keyboard, KEYBOARD_A_USAGE, 0),
                library.aoahid_mouse_button(session.mouse, 1, 1),  # Caller selects pointer button 1.
            )
        )
        if status == AOAHID_OK:  # noqa: F405
            ready.append(session)
        elif status == AOAHID_ERR_NO_DEVICE:  # noqa: F405
            close_device(library, session, timeout_ms)
        else:
            report_error(library, status, "keyboard/mouse continuation")
            result = status
    for session in ready:
        status = first_failure(
            library.aoahid_node_submit_blocking(node, timeout_ms)
            for node in (session.keyboard, session.mouse)
        )
        if status != AOAHID_OK:  # noqa: F405
            result = status
    return result


def run_shared(library_path: str, timeout_ms: int) -> int:
    library = load(library_path)  # noqa: F405
    options = context_options()
    context = ContextP()  # noqa: F405
    status = library.aoahid_context_create(c.byref(options), c.byref(context))
    if status != AOAHID_OK:  # noqa: F405
        report_error(library, status, "context create")
        return int(status)
    specs: Optional[Specs] = None
    sessions: list[Session] = []
    result = AOAHID_OK  # noqa: F405
    try:
        specs = create_specs(library)
        snapshot = discover(library, context, timeout_ms)
        try:
            for index in range(discovery_count(library, snapshot)):
                info = discovery_info(library, snapshot, index)
                print_info(info)
                locator = locator_from(info)
                if locator is None:
                    print(
                        "stable device locator: caller policy requires a physical port path",
                        file=sys.stderr,
                    )
                    if result == AOAHID_OK:  # noqa: F405
                        result = AOAHID_ERR_UNSUPPORTED  # noqa: F405
                    continue
                session, open_status = open_session(
                    library, context, info, locator, specs, timeout_ms
                )
                if session is not None:
                    sessions.append(session)
                elif result == AOAHID_OK:  # noqa: F405
                    result = open_status
        finally:
            library.aoahid_discovery_destroy(snapshot)
        disappeared, drive_status = update_then_submit(library, sessions, timeout_ms)
        if result == AOAHID_OK:  # noqa: F405
            result = drive_status
        # Discovery and re-registration touch only missing sessions; every live
        # sibling remains open and continues to own its keyboard/mouse state.
        reopen_status = reopen(library, context, sessions, disappeared, specs, timeout_ms)
        if result == AOAHID_OK:  # noqa: F405
            result = reopen_status
        continuation = continue_without_touch(library, context, sessions, timeout_ms)
        if result == AOAHID_OK:  # noqa: F405
            result = continuation
    except AoaHidFailure as error:
        print(error, file=sys.stderr)
        result = error.status
    finally:
        for session in sessions:
            close_device(library, session, timeout_ms)
        if specs is not None:
            release_specs(library, specs)
        close = destroy_context(library, context, timeout_ms)
        if result == AOAHID_OK and close != AOAHID_OK:  # noqa: F405
            result = close
    return int(result)


def enumerate_locators(library: c.CDLL, timeout_ms: int) -> tuple[int, list[Locator]]:
    options = context_options()
    context = ContextP()  # noqa: F405
    status = library.aoahid_context_create(c.byref(options), c.byref(context))
    if status != AOAHID_OK:  # noqa: F405
        return int(status), []
    locators: list[Locator] = []
    try:
        snapshot = discover(library, context, timeout_ms)
        try:
            for index in range(discovery_count(library, snapshot)):
                info = discovery_info(library, snapshot, index)
                print_info(info)
                locator = locator_from(info)
                if locator is None:
                    print(
                        "stable device locator: caller policy requires a physical port path",
                        file=sys.stderr,
                    )
                    status = AOAHID_ERR_UNSUPPORTED  # noqa: F405
                    break
                locators.append(locator)
        finally:
            library.aoahid_discovery_destroy(snapshot)
    finally:
        close = destroy_context(library, context, timeout_ms)
        if status == AOAHID_OK:  # noqa: F405
            status = close
    if status != AOAHID_OK:  # noqa: F405
        locators.clear()
    return int(status), locators


def run_worker(library_path: str, locator: Locator, timeout_ms: int) -> int:
    library = load(library_path)  # noqa: F405
    options = context_options()
    context = ContextP()  # noqa: F405
    status = library.aoahid_context_create(c.byref(options), c.byref(context))
    if status != AOAHID_OK:  # noqa: F405
        return int(status)
    specs: Optional[Specs] = None
    sessions: list[Session] = []
    result = AOAHID_OK  # noqa: F405
    try:
        specs = create_specs(library)
        snapshot = discover(library, context, timeout_ms)
        found = False
        try:
            for index in range(discovery_count(library, snapshot)):
                info = discovery_info(library, snapshot, index)
                if locator_matches(locator, info):
                    found = True
                    session, open_status = open_session(
                        library, context, info, locator, specs, timeout_ms
                    )
                    if session is not None:
                        sessions.append(session)
                    else:
                        result = open_status
                    break
        finally:
            library.aoahid_discovery_destroy(snapshot)
        if not found and result == AOAHID_OK:  # noqa: F405
            result = AOAHID_ERR_NO_DEVICE  # noqa: F405
        disappeared, drive_status = update_then_submit(library, sessions, timeout_ms)
        if result == AOAHID_OK:  # noqa: F405
            result = drive_status
        # A missing device is rediscovered on its own worker/context; sibling
        # threads continue independently and share no synchronization domain.
        reopen_status = reopen(library, context, sessions, disappeared, specs, timeout_ms)
        if result == AOAHID_OK:  # noqa: F405
            result = reopen_status
        continuation = continue_without_touch(library, context, sessions, timeout_ms)
        if result == AOAHID_OK:  # noqa: F405
            result = continuation
    except AoaHidFailure as error:
        print(error, file=sys.stderr)
        result = error.status
    finally:
        for session in sessions:
            close_device(library, session, timeout_ms)
        if specs is not None:
            release_specs(library, specs)
        close = destroy_context(library, context, timeout_ms)
        if result == AOAHID_OK and close != AOAHID_OK:  # noqa: F405
            result = close
    return int(result)


def run_threaded(library_path: str, timeout_ms: int) -> int:
    library = load(library_path)  # noqa: F405
    try:
        status, locators = enumerate_locators(library, timeout_ms)
    except AoaHidFailure as error:
        print(error, file=sys.stderr)
        return int(error.status)
    if status != AOAHID_OK:  # noqa: F405
        return status
    results: list[int] = []
    result_lock = threading.Lock()

    def worker(locator: Locator) -> None:
        result = run_worker(library_path, locator, timeout_ms)
        with result_lock:
            results.append(result)

    # The count comes only from discovery; no device-count ceiling is encoded.
    threads = [threading.Thread(target=worker, args=(locator,), daemon=False) for locator in locators]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    return next((value for value in results if value != AOAHID_OK), AOAHID_OK)  # noqa: F405


def main() -> int:
    if len(sys.argv) != 4 or sys.argv[3] not in ("shared", "threaded"):
        print(
            "usage: multi_profile.py LIBAOAHID_PATH CONTROL_TIMEOUT_MS shared|threaded",
            file=sys.stderr,
        )
        return 2
    timeout_ms = int(sys.argv[2])
    if timeout_ms <= 0:
        print("CONTROL_TIMEOUT_MS must be positive", file=sys.stderr)
        return 2
    return (
        run_shared(sys.argv[1], timeout_ms)
        if sys.argv[3] == "shared"
        else run_threaded(sys.argv[1], timeout_ms)
    )


if __name__ == "__main__":
    raise SystemExit(main())
