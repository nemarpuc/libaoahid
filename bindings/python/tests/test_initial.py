#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libaoahid contributors
"""aoahid.initial must equal what the compiled C aoahid_*_init functions leave, byte for byte.

AOAHID_INIT_ORACLE names the built aoahid_abi_init_oracle program. Pointer fields are compared by
being null or not and, for strings, by content, since their addresses differ between processes.
"""

from __future__ import annotations

import ctypes as c
import os
import subprocess
import sys

from aoahid import initial


def pointer_spans(cls, base: int = 0):
    """(offset, size) of every pointer field of a ctypes structure, nested ones included."""
    spans = []
    for name, kind, *_ in cls._fields_:
        offset = base + getattr(cls, name).offset
        if issubclass(kind, c.Structure):
            spans += pointer_spans(kind, offset)
        elif issubclass(kind, c.Array) and issubclass(kind._type_, c.Structure):
            for i in range(kind._length_):
                spans += pointer_spans(kind._type_, offset + i * c.sizeof(kind._type_))
        elif kind is c.c_char_p or kind is c.c_void_p or issubclass(kind, c._Pointer) or kind.__name__ == "CFunctionType":
            spans.append((offset, c.sizeof(kind)))
        elif c.sizeof(kind) == c.sizeof(c.c_void_p) and issubclass(kind, c._CFuncPtr):
            spans.append((offset, c.sizeof(kind)))
    return spans


def masked(raw: bytes, spans) -> bytes:
    data = bytearray(raw)
    for offset, size in spans:
        data[offset:offset + size] = b"\x01" * size if any(raw[offset:offset + size]) else b"\x00" * size
    return bytes(data)


def main() -> int:
    path = os.environ.get("AOAHID_INIT_ORACLE")
    if not path:
        raise SystemExit("AOAHID_INIT_ORACLE is not set")
    text = subprocess.run([path], check=True, capture_output=True, text=True).stdout
    lines = dict(line.split("=", 1) for line in text.splitlines())
    expected = {k: bytes.fromhex(v) for k, v in lines.items() if k.endswith("_options")}
    failures = []
    gamepad = initial.gamepad_options()
    for i in range(gamepad.axis_count):
        axis = gamepad.axes[i]
        seen = (f"{axis.role},{axis.usage_page},{axis.usage},{axis.value.logical_minimum},"
                f"{axis.value.logical_maximum},{axis.value.bit_width},{axis.neutral_value},"
                f"{axis.expected_linux_code.decode()},{axis.expected_android_axis.decode()}")
        if lines.get(f"gamepad_axis{i}") != seen:
            failures.append(f"gamepad_axis{i}: {seen} != {lines.get(f'gamepad_axis{i}')}")
    pen = initial.pen_options()
    if lines["pen_barrel"] != f"{pen.barrel_usage_count},{pen.barrel_usages[0]}":
        failures.append("pen_barrel differs")
    strings = initial.accessory_options().strings
    if lines["accessory"] != f"{strings.manufacturer.decode()},{strings.model.decode()}":
        failures.append("accessory strings differ")
    for name, want in expected.items():
        got = getattr(initial, name)()
        raw = bytes(got)
        if len(raw) != len(want):
            failures.append(f"{name}: size {len(raw)} != {len(want)}")
        elif masked(raw, pointer_spans(type(got))) != masked(want, pointer_spans(type(got))):
            failures.append(f"{name}: bytes differ")
    initial_names = {n for n in initial.__all__ if n.endswith("_options")}
    if initial_names != set(expected):
        failures.append(f"covered {sorted(set(expected) ^ initial_names)} differ")
    for failure in failures:
        print("initial mismatch:", failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
