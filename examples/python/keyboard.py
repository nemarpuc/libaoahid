#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libaoahid contributors
"""Press and release the letter "a" on the one Android phone that is plugged in.

Open a text field on the phone first. The values are those of aoahid.initial (the same as the C
aoahid_*_init functions); keyboard_raw.py is the same program with every call and value spelled out.
The phone must already take AOA 2.0 HID requests; docs/QUICKSTART.md says how to get there.
"""

import sys

import aoahid

KEY_A = 0x04  # HID Keyboard page usage

if len(sys.argv) != 2:
    sys.exit("usage: keyboard.py LIBAOAHID_PATH")

try:
    with aoahid.Session(sys.argv[1]) as session:
        keyboard = session.open_only().keyboard()
        keyboard.tap(KEY_A)
except aoahid.Error as error:
    sys.exit(str(error))
