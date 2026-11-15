#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libaoahid contributors
"""Press and release the letter "a" on the first Android phone found.

Open a text field on the phone first. Every value below is the program's own choice; none is a
default of the library (the limits they sit within are in docs/LIMITS.md). The phone must
already take AOA 2.0 HID requests; docs/QUICKSTART.md says how to get there and what to do when
the first aoahid_node_open fails with AOAHID_ERR_STALL.
"""

from __future__ import annotations

import ctypes as c
import sys
import time

from aoahid import Error, check
from aoahid.native import (
    AOAHID_EVENT_CALLER_POLL,
    AOAHID_INTERFACE_CLAIM_NONE,
    AOAHID_LOG_DISABLED,
    ContextOptions,
    ContextP,
    DeviceOptions,
    DeviceP,
    DiscoveryP,
    KeyboardOptions,
    NodeOptions,
    NodeP,
    SpecP,
    load,
)

KEY_A = 0x04  # HID Keyboard page usage
TIMEOUT_MS = 500


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: keyboard.py LIBAOAHID_PATH", file=sys.stderr)
        return 2
    library = load(sys.argv[1])

    context_options = ContextOptions()
    context_options.event_mode = AOAHID_EVENT_CALLER_POLL
    context_options.log_level = AOAHID_LOG_DISABLED

    device_options = DeviceOptions()
    device_options.descriptor_fragment_bytes = 4096
    device_options.transfer_pool_slots = 4
    device_options.maximum_report_bytes = 1024
    device_options.aoa_descriptor_wire_policy_bytes = 4096
    device_options.linux_hid.descriptor_bytes = 4096
    device_options.linux_hid.fields_per_report = 256
    device_options.linux_hid.global_stack_depth = 4
    device_options.linux_hid.report_size_bits = 256
    device_options.linux_hid.usages = 12288
    device_options.linux_hid.report_data_bits = 65528
    device_options.target_ep0_data_policy_bytes = 4096
    device_options.host_control_buffer_policy_bytes = 4096
    device_options.interface_claim_policy = AOAHID_INTERFACE_CLAIM_NONE
    device_options.interface_number = -1

    keyboard = KeyboardOptions()
    keyboard.usage_minimum = 0x04
    keyboard.usage_maximum = 0xDD

    node_options = NodeOptions()  # no reserved transfer slot

    context = ContextP()
    discovery = DiscoveryP()
    device = DeviceP()
    spec = SpecP()
    node = NodeP()
    try:
        check(library, library.aoahid_context_create(c.byref(context_options), c.byref(context)))
        check(library, library.aoahid_discover(context, TIMEOUT_MS, c.byref(discovery)))
        if library.aoahid_discovery_count(discovery) == 0:
            print("no USB device found", file=sys.stderr)
            return 1
        info = library.aoahid_discovery_get(discovery, 0)
        check(library, library.aoahid_device_open(context, info, c.byref(device_options),
                                                  c.byref(device)))
        check(library, library.aoahid_spec_create_keyboard(c.byref(keyboard), c.byref(spec)))
        check(library, library.aoahid_node_open(device, spec, c.byref(node_options),
                                                TIMEOUT_MS, c.byref(node)))
        # Android registers the HID without telling the host; a report sent at once may be lost.
        time.sleep(1)
        for down in (1, 0):
            check(library, library.aoahid_keyboard_key(node, KEY_A, down))
            check(library, library.aoahid_node_submit_blocking(node, TIMEOUT_MS))
            time.sleep(0.05)
    except Error as error:
        print(error, file=sys.stderr)
        return 1
    finally:
        if discovery:
            library.aoahid_discovery_destroy(discovery)
        if spec:
            library.aoahid_spec_release(spec)
        if node:
            library.aoahid_node_close(node, TIMEOUT_MS, TIMEOUT_MS)
        if device:
            library.aoahid_device_close(device, TIMEOUT_MS, TIMEOUT_MS)
        if context:
            library.aoahid_context_destroy_blocking(context, TIMEOUT_MS, TIMEOUT_MS, TIMEOUT_MS)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
