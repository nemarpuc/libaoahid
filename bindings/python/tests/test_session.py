#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libaoahid contributors
"""Session, Device and Node against the fake libusb build of the shared library.

AOAHID_FAKE_LIBRARY names a libaoahid built with AOAHID_USE_FAKE_LIBUSB=ON. No phone is needed.
"""

from __future__ import annotations

import ctypes as c
import os
import sys

import aoahid
from aoahid import initial


class FakeDevice(c.Structure):
    _fields_ = [("bus", c.c_uint8), ("address", c.c_uint8), ("port_path", c.c_char_p),
                ("port_path_length", c.c_size_t), ("vendor_id", c.c_uint16), ("product_id", c.c_uint16),
                ("protocol_version", c.c_uint16), ("serial", c.c_char_p), ("product", c.c_char_p),
                ("open_status", c.c_int)]


def add_phone(session: aoahid.Session) -> None:
    session.library.aoahid_fake_libusb_reset()
    path = (c.c_uint8 * 2)(3, 4)
    phone = FakeDevice(1, 5, c.cast(path, c.c_char_p), 2, 0x18D1, 0x2D00, 2, b"fake-serial", b"Android", 0)
    assert session.library.aoahid_fake_libusb_add_device(c.byref(phone)) >= 0
    session._keep = (path, phone)


def expect(error_type, call) -> None:
    try:
        call()
    except error_type:
        return
    raise AssertionError("expected " + error_type.__name__)


def main() -> int:
    library_path = os.environ.get("AOAHID_FAKE_LIBRARY")
    if not library_path:
        raise SystemExit("AOAHID_FAKE_LIBRARY is not set")

    with aoahid.Session(library_path) as session:
        expect(aoahid.Error, session.open_only)  # no device: nothing is picked
        add_phone(session)
        assert session.discover()[0]["vendor_id"] == 0x18D1
        device = session.open_only()
        keyboard = device.keyboard(settle=0)
        keyboard.tap(0x04, hold_seconds=0)
        mouse = device.mouse(settle=0)
        mouse.move(5, -5)
        mouse.button(1, True)
        mouse.submit_blocking()
        assert keyboard.raw and mouse.raw

    # Everything is released in reverse order, and closing twice is harmless.
    assert not keyboard.raw and not mouse.raw and not device.raw and not session.raw
    session.close()

    # A raw structure replaces the keyword values; an invalid one is refused with the library's error.
    with aoahid.Session(library_path) as session:
        add_phone(session)
        device = session.open_only()
        bad = initial.keyboard_options(usage_minimum=0)
        expect(aoahid.Error, lambda: device.keyboard(bad, settle=0))
        device.keyboard(initial.keyboard_options(usage_maximum=0x65), settle=0)

    # A library of another version is refused before anything is created.
    real = aoahid.native.AOAHID_VERSION_PATCH
    aoahid.native.AOAHID_VERSION_PATCH = real + 1
    try:
        expect(aoahid.Error, lambda: aoahid.Session(library_path))
    finally:
        aoahid.native.AOAHID_VERSION_PATCH = real
    return 0


if __name__ == "__main__":
    sys.exit(main())
