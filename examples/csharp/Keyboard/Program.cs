// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
// Presses and releases the letter "a" on the one Android phone that is plugged in.
//
// Open a text field on the phone first. The values are those of the Init() methods (the same as the
// C aoahid_*_init functions); ../KeyboardRaw is the same program with every call and value spelled
// out. The phone must already take AOA 2.0 HID requests; docs/QUICKSTART.md says how to get there.
using AoaHid;

const ushort KeyA = 0x04; // HID Keyboard page usage

try
{
    using var session = new AoaSession();
    var keyboard = session.OpenOnly().Keyboard();
    keyboard.Tap(KeyA);
}
catch (AoaHidException error)
{
    Console.Error.WriteLine(error.Message);
    return 1;
}
return 0;
