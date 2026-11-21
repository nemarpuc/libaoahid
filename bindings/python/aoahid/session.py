# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libaoahid contributors
"""Owning objects over the raw ABI: a Context, a Device and its Nodes, released in reverse order.

Nothing is picked for the caller: ``open_only`` refuses unless exactly one USB device is present.
Calls are not thread-safe, as the C ABI requires (the application serializes calls on a Context).
Every object has ``.raw`` (the ctypes handle) so a program can drop to the raw calls at any point.
"""

from __future__ import annotations

import contextlib
import ctypes as c
import time
from typing import Callable, Optional, cast

from . import initial, native as n

__all__ = ["Session", "Device", "Node"]

# docs/API.md "Open sequence" step 5: nothing tells the host when Android has registered the HID,
# so leave a short gap (the README's first program waits 100 ms).
SETTLE_SECONDS = 0.1


def _error(field: str, reason: str) -> n.Error:
    return n.Error(n.AOAHID_ERR_UNSUPPORTED, "AOAHID_ERR_UNSUPPORTED", field, reason)


class _Owner:
    """Closes what it opened, newest first, and keeps closing when one close fails."""

    def __init__(self) -> None:
        self._children: list = []

    def _adopt(self, child) -> None:
        self._children.append(child)

    def _close_children(self) -> None:
        while self._children:
            self._children.pop().close()


class Session(_Owner):
    """A loaded library and a Context (internal event thread, no logging unless options say so)."""

    def __init__(self, library_path, *, options: Optional[n.ContextOptions] = None,
                 timeout_ms: int = n.AOAHID_CONTROL_TIMEOUT_MS) -> None:
        super().__init__()
        self.library = n.load(library_path)
        expected = (n.AOAHID_VERSION_MAJOR << 24) | (n.AOAHID_VERSION_MINOR << 16) | n.AOAHID_VERSION_PATCH
        found = self.library.aoahid_version()
        if found != expected:
            raise _error("version", f"the library is version {found >> 24}.{(found >> 16) & 0xFF}.{found & 0xFFFF}, "
                         f"this package is {n.AOAHID_VERSION_MAJOR}.{n.AOAHID_VERSION_MINOR}."
                         f"{n.AOAHID_VERSION_PATCH}")
        self.timeout_ms = timeout_ms
        self.raw = n.ContextP()
        options = options if options is not None else initial.context_options()
        self._check(self.library.aoahid_context_create(c.byref(options), c.byref(self.raw)))
        self._discovery = n.DiscoveryP()

    def _check(self, result: int) -> int:
        return n.check(self.library, result)

    def discover(self) -> list[dict]:
        """The USB devices found now, as plain dicts; ``open(index)`` opens one of them."""
        self._drop_discovery()
        self._check(self.library.aoahid_discover(self.raw, self.timeout_ms, c.byref(self._discovery)))
        found = []
        for index in range(self.library.aoahid_discovery_count(self._discovery)):
            info = self.library.aoahid_discovery_get(self._discovery, index).contents
            found.append({"vendor_id": info.vendor_id, "product_id": info.product_id,
                          "serial": (info.serial or b"").decode("utf-8", "replace"),
                          "product": (info.product or b"").decode("utf-8", "replace")})
        return found

    def open(self, index: int, options: Optional[n.DeviceOptions] = None) -> "Device":
        """Open device ``index`` of the last ``discover()``."""
        if not self._discovery or index >= self.library.aoahid_discovery_count(self._discovery):
            raise IndexError("no such device in the last discover()")
        info = self.library.aoahid_discovery_get(self._discovery, index)
        device = Device(self, info, options or initial.device_options())
        self._adopt(device)
        return device

    def open_only(self, options: Optional[n.DeviceOptions] = None) -> "Device":
        """Discover and open the one device present; any other count is an error."""
        found = self.discover()
        if len(found) != 1:
            raise _error("devices", f"expected exactly one USB device, found {len(found)}; "
                         "use discover() and open(index)")
        return self.open(0, options)

    def _drop_discovery(self) -> None:
        if self._discovery:
            self.library.aoahid_discovery_destroy(self._discovery)
            self._discovery = n.DiscoveryP()

    def close(self) -> None:
        self._close_children()
        self._drop_discovery()
        if self.raw:
            t = self.timeout_ms
            self.library.aoahid_context_destroy_blocking(self.raw, t, t, t)
            self.raw = n.ContextP()

    def __enter__(self) -> "Session":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


class Device(_Owner):
    def __init__(self, session: Session, info, options: n.DeviceOptions) -> None:
        super().__init__()
        self.session = session
        self._registrations = 0
        self.raw = n.DeviceP()
        session._check(session.library.aoahid_device_open(session.raw, info, c.byref(options),
                                                          c.byref(self.raw)))

    def _timeouts(self) -> n.CommitTimeouts:
        return initial.commit_timeouts(control_timeout_ms=self.session.timeout_ms)

    @contextlib.contextmanager
    def batch(self):
        """Stage Node opens and closes; they reach the phone together when the block ends."""
        library = self.session.library
        self.session._check(library.aoahid_device_batch_begin(self.raw))
        try:
            yield self
        finally:
            self.session._check(library.aoahid_device_batch_end(self.raw, c.byref(self._timeouts())))

    def node(self, kind: str, options=None, *, registration: Optional[int] = None,
             settle: float = SETTLE_SECONDS) -> "Node":
        """Open a Node of ``kind`` (a key of ``NODE_KINDS``), then wait ``settle`` seconds.

        Nodes given the same ``registration`` number share one HID device on the phone (each
        needs its own Report ID). Without one, the Node is a HID device of its own.
        """
        if registration is None:
            self._registrations += 1
            registration = self._registrations
        make, create, cls = NODE_KINDS[kind]
        options = options if options is not None else make()
        library = self.session.library
        spec = n.SpecP()
        self.session._check(getattr(library, create)(c.byref(options), c.byref(spec)))
        try:
            raw = n.NodeP()
            placement = initial.node_placement(registration=registration)
            self.session._check(library.aoahid_node_open(self.raw, spec, c.byref(placement),
                                                         c.byref(self._timeouts()), c.byref(raw)))
        finally:
            library.aoahid_spec_release(spec)  # the Node keeps its own reference
        node = cls(self, raw)
        self._adopt(node)
        if settle:
            time.sleep(settle)
        return node

    def keyboard(self, options=None, **kw) -> "KeyboardNode":
        return cast(KeyboardNode, self.node("keyboard", options, **kw))

    def mouse(self, options=None, **kw) -> "MouseNode":
        return cast(MouseNode, self.node("mouse", options, **kw))

    def close(self) -> None:
        self._close_children()
        if self.raw:
            t = self.session.timeout_ms
            self.session.library.aoahid_device_close(self.raw, t, n.AOAHID_DRAIN_TIMEOUT_MS)
            self.raw = n.DeviceP()


class Node:
    def __init__(self, device: Device, raw) -> None:
        self.device = device
        self.raw = raw
        self._library = device.session.library
        self._check = device.session._check

    def submit(self, timeout_ms: int = n.AOAHID_SEND_TIMEOUT_MS) -> None:
        """Queue the pending state without waiting for it to be sent."""
        self._check(self._library.aoahid_node_submit(self.raw, timeout_ms))

    def submit_blocking(self, timeout_ms: int = n.AOAHID_SEND_TIMEOUT_MS) -> None:
        self._check(self._library.aoahid_node_submit_blocking(self.raw, timeout_ms))

    def battery(self, strength: int) -> None:
        self._check(self._library.aoahid_battery_update(self.raw, strength))

    def close(self) -> None:
        if self.raw:
            self._library.aoahid_node_close(self.raw, c.byref(self.device._timeouts()))
            self.raw = None


class KeyboardNode(Node):
    def key(self, usage: int, down: bool) -> None:
        self._check(self._library.aoahid_keyboard_key(self.raw, usage, int(down)))

    def tap(self, usage: int, hold_seconds: float = 0.05) -> None:
        """Press, send, wait ``hold_seconds``, release, send."""
        self.key(usage, True)
        self.submit_blocking()
        time.sleep(hold_seconds)
        self.key(usage, False)
        self.submit_blocking()


class MouseNode(Node):
    def move(self, dx: int, dy: int) -> None:
        self._check(self._library.aoahid_mouse_move(self.raw, dx, dy))

    def scroll(self, wheel: int, pan: int = 0) -> None:
        self._check(self._library.aoahid_mouse_scroll(self.raw, wheel, pan))

    def button(self, button: int, down: bool) -> None:
        self._check(self._library.aoahid_mouse_button(self.raw, button, int(down)))


class ToggleNode(Node):
    def usage(self, usage: int, down: bool) -> None:
        self._check(self._library.aoahid_toggle_usage(self.raw, usage, int(down)))


class GamepadNode(Node):
    def button(self, button: int, down: bool) -> None:
        self._check(self._library.aoahid_gamepad_button(self.raw, button, int(down)))

    def axis(self, axis_index: int, value: int) -> None:
        self._check(self._library.aoahid_gamepad_axis(self.raw, axis_index, value))

    def dpad(self, up: bool, down: bool, right: bool, left: bool) -> None:
        self._check(self._library.aoahid_gamepad_dpad(self.raw, int(up), int(down), int(right), int(left)))


class _ContactNode(Node):
    _contact: str

    def contact(self, contact_id: int, down: bool, x: int, y: int, extra=None) -> None:
        call = getattr(self._library, self._contact)
        self._check(call(self.raw, contact_id, int(down), x, y,
                         None if extra is None else c.byref(extra)))


class TouchscreenNode(_ContactNode):
    _contact = "aoahid_touchscreen_contact"


class TouchpadNode(_ContactNode):
    _contact = "aoahid_touchpad_contact"

    def button(self, button: int, down: bool) -> None:
        self._check(self._library.aoahid_touchpad_button(self.raw, button, int(down)))


class PenNode(Node):
    def update(self, sample: n.PenSample) -> None:
        self._check(self._library.aoahid_pen_update(self.raw, c.byref(sample)))


class DigitizerNode(Node):
    def update(self, sample: n.DigitizerSample) -> None:
        self._check(self._library.aoahid_digitizer_update(self.raw, c.byref(sample)))


class RawNode(Node):
    def report(self, data: bytes, timeout_ms: int = n.AOAHID_SEND_TIMEOUT_MS) -> None:
        buffer = (c.c_uint8 * len(data)).from_buffer_copy(data)
        self._check(self._library.aoahid_raw_submit(self.raw, buffer, len(data), timeout_ms))


def _raw_options() -> n.RawOptions:
    raise TypeError("a raw Node needs its own descriptor: pass options=initial.raw_options(...) filled in")


NODE_KINDS: dict[str, tuple[Callable, str, type]] = {
    "keyboard": (initial.keyboard_options, "aoahid_spec_create_keyboard", KeyboardNode),
    "mouse": (initial.mouse_options, "aoahid_spec_create_mouse", MouseNode),
    "toggle": (initial.toggle_options, "aoahid_spec_create_toggle", ToggleNode),
    "gamepad": (initial.gamepad_options, "aoahid_spec_create_gamepad", GamepadNode),
    "touchscreen": (initial.touchscreen_options, "aoahid_spec_create_touchscreen", TouchscreenNode),
    "touchpad": (initial.touchpad_options, "aoahid_spec_create_touchpad", TouchpadNode),
    "pen": (initial.pen_options, "aoahid_spec_create_pen", PenNode),
    "digitizer": (initial.digitizer_options, "aoahid_spec_create_digitizer", DigitizerNode),
    "raw": (_raw_options, "aoahid_spec_create_raw", RawNode),
}
