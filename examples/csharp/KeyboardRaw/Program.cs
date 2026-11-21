// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
// The raw-ABI form of ../Keyboard: every call and value spelled out.
// Presses and releases the letter "a" on the first Android phone found.
//
// Open a text field on the phone first. Every value below is the program's own choice; none is a
// default of the library (the limits they sit within are in docs/LIMITS.md). The phone must
// already take AOA 2.0 HID requests; docs/QUICKSTART.md says how to get there and what to do when
// the first aoahid_node_open fails with AOAHID_ERR_STALL.
using System.Runtime.InteropServices;
using AoaHid;

const ushort KeyA = 0x04; // HID Keyboard page usage
const uint TimeoutMs = 500;

nint context = 0, discovery = 0, device = 0, spec = 0, node = 0;
try
{
    var contextOptions = ContextOptions.Init();
    contextOptions.EventMode = EventMode.CallerPoll;
    contextOptions.LogLevel = LogLevel.Disabled;

    var deviceOptions = DeviceOptions.Init();
    deviceOptions.DescriptorFragmentBytes = 4096;
    deviceOptions.TransferPoolSlots = 4;
    deviceOptions.MaximumReportBytes = 1024;
    deviceOptions.AoaDescriptorWirePolicyBytes = 4096;
    deviceOptions.LinuxHid.DescriptorBytes = 4096;
    deviceOptions.LinuxHid.FieldsPerReport = 256;
    deviceOptions.LinuxHid.GlobalStackDepth = 4;
    deviceOptions.LinuxHid.ReportSizeBits = 256;
    deviceOptions.LinuxHid.Usages = 12288;
    deviceOptions.LinuxHid.ReportDataBits = 65528;
    deviceOptions.TargetEp0DataPolicyBytes = 4096;
    deviceOptions.HostControlBufferPolicyBytes = 4096;
    deviceOptions.InterfaceClaimPolicy = ClaimPolicy.None;
    deviceOptions.InterfaceNumber = -1;

    var keyboard = KeyboardOptions.Init();
    keyboard.UsageMinimum = 0x04;
    keyboard.UsageMaximum = 0xDD;

    var placement = NodePlacement.Init(); // no reserved transfer slot
    placement.Registration = 1; // its own HID device
    var timeouts = CommitTimeouts.Init();
    timeouts.ControlTimeoutMs = TimeoutMs;
    timeouts.DrainTimeoutMs = TimeoutMs;

    AoaHidException.Check(Native.ContextCreate(in contextOptions, out context));
    AoaHidException.Check(Native.Discover(context, TimeoutMs, out discovery));
    if (Native.DiscoveryCount(discovery) == 0)
    {
        Console.Error.WriteLine("no USB device found");
        return 1;
    }
    var info = Marshal.PtrToStructure<DeviceInfo>(Native.DiscoveryGet(discovery, 0));
    AoaHidException.Check(Native.DeviceOpen(context, in info, in deviceOptions, out device));
    AoaHidException.Check(Native.SpecCreateKeyboard(in keyboard, out spec));
    AoaHidException.Check(Native.NodeOpen(device, spec, in placement, in timeouts, out node));
    // Android registers the HID without telling the host; a report sent at once may be lost.
    Thread.Sleep(1000);
    foreach (var down in new uint[] { 1, 0 })
    {
        AoaHidException.Check(Native.KeyboardKey(node, KeyA, down));
        AoaHidException.Check(Native.NodeSubmitBlocking(node, TimeoutMs));
        Thread.Sleep(50);
    }
    return 0;
}
catch (AoaHidException error)
{
    Console.Error.WriteLine(error.Message);
    return 1;
}
finally
{
    if (discovery != 0)
        Native.DiscoveryDestroy(discovery);
    if (spec != 0)
        Native.SpecRelease(spec);
    if (node != 0)
        Native.NodeClose(node, in timeouts);
    if (device != 0)
        Native.DeviceClose(device, TimeoutMs, TimeoutMs);
    if (context != 0)
        Native.ContextDestroyBlocking(context, TimeoutMs, TimeoutMs, TimeoutMs);
}
