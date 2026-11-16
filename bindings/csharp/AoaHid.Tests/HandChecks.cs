// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
// The checks of the hand-written code: each Init() against the bytes the compiled C aoahid_*_init
// function leaves (args[1] names the built aoahid_abi_init_oracle; pointer fields are compared by
// being null or not, and by what they point at), and AoaSession against the fake libusb build of
// the shared library (the native loader must find it as "aoahid").
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using AoaHid;

internal static class HandChecks
{
    public static List<string> Run(string[] args)
    {
        var failures = CheckInitial(args);
        failures.AddRange(CheckSession());
        return failures;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct FakeDevice
    {
        public byte Bus;
        public byte Address;
        public nint PortPath;
        public nuint PortPathLength;
        public ushort VendorId;
        public ushort ProductId;
        public ushort ProtocolVersion;
        public nint Serial;
        public nint Product;
        public int OpenStatus;
    }

    [DllImport("aoahid", EntryPoint = "aoahid_fake_libusb_reset")]
    private static extern void FakeReset();

    [DllImport("aoahid", EntryPoint = "aoahid_fake_libusb_add_device")]
    private static extern int FakeAddDevice(in FakeDevice device);

    private static void AddPhone()
    {
        FakeReset();
        var path = Marshal.AllocHGlobal(2);
        Marshal.WriteByte(path, 0, 3);
        Marshal.WriteByte(path, 1, 4);
        var phone = new FakeDevice
        {
            Bus = 1, Address = 5, PortPath = path, PortPathLength = 2, VendorId = 0x18D1, ProductId = 0x2D00,
            ProtocolVersion = 2, Serial = Marshal.StringToCoTaskMemUTF8("fake-serial"),
            Product = Marshal.StringToCoTaskMemUTF8("Android"), OpenStatus = 0,
        };
        if (FakeAddDevice(in phone) < 0)
            throw new InvalidOperationException("the fake libusb refused the device");
    }

    private static List<string> CheckSession()
    {
        var failures = new List<string>();
        try
        {
            using (var session = new AoaSession())
            {
                FakeReset();
                try
                {
                    session.OpenOnly();
                    failures.Add("OpenOnly with no device did not throw");
                }
                catch (AoaHidException)
                {
                }
                AddPhone();
                var device = session.OpenOnly();
                var keyboard = device.Keyboard(settle: TimeSpan.Zero);
                keyboard.Tap(0x04, TimeSpan.Zero);
                var mouse = device.Mouse(settle: TimeSpan.Zero);
                mouse.Move(5, -5);
                mouse.Button(1, true);
                mouse.SubmitBlocking();
            }
            using (var session = new AoaSession())
            {
                AddPhone();
                var device = session.OpenOnly();
                var bad = KeyboardOptions.Init();
                bad.UsageMinimum = 0;
                try
                {
                    device.Keyboard(bad, TimeSpan.Zero);
                    failures.Add("an invalid keyboard was accepted");
                }
                catch (AoaHidException)
                {
                }
            }
        }
        catch (Exception error)
        {
            failures.Add("session: " + error);
        }
        return failures;
    }

    private static List<string> CheckInitial(string[] args)
    {
        var failures = new List<string>();
        var start = new ProcessStartInfo(args[1]) { RedirectStandardOutput = true };
        using var process = Process.Start(start)!;
        var text = process.StandardOutput.ReadToEnd();
        process.WaitForExit();
        var lines = text.Split('\n', StringSplitOptions.RemoveEmptyEntries).Select(l => l.Split('=', 2)).ToDictionary(p => p[0], p => p[1]);

        void Compare<T>(string name, T value, params long[] pointers) where T : struct
        {
            var size = Marshal.SizeOf<T>();
            var mine = new byte[size];
            var block = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.StructureToPtr(value, block, false);
                Marshal.Copy(block, mine, 0, size);
            }
            finally
            {
                Marshal.FreeHGlobal(block);
            }
            var theirs = Convert.FromHexString(lines[name]);
            if (theirs.Length != size)
            {
                failures.Add($"{name}: size {size} != {theirs.Length}");
                return;
            }
            foreach (var offset in pointers)
            {
                foreach (var data in new[] { mine, theirs })
                {
                    var set = data.Skip((int)offset).Take(IntPtr.Size).Any(b => b != 0);
                    Array.Clear(data, (int)offset, IntPtr.Size);
                    data[offset] = (byte)(set ? 1 : 0);
                }
            }
            if (!mine.SequenceEqual(theirs))
                failures.Add($"{name}: bytes differ");
        }

        long At<T>(string field) => (long)Marshal.OffsetOf<T>(field);

        Compare("context_options", ContextOptions.Init(), At<ContextOptions>("LogSink"), At<ContextOptions>("LogUser"));
        Compare("device_options", DeviceOptions.Init());
        Compare("accessory_options", AccessoryOptions.Init(),
                At<AccessoryOptions>("Strings") + At<AoaStrings>("Manufacturer"),
                At<AccessoryOptions>("Strings") + At<AoaStrings>("Model"));
        Compare("channel_options", ChannelOptions.Init());
        Compare("node_options", NodeOptions.Init());
        Compare("keyboard_options", KeyboardOptions.Init());
        Compare("mouse_options", MouseOptions.Init());
        Compare("toggle_options", ToggleOptions.Init(), At<ToggleOptions>("AllowedUsages"),
                At<ToggleOptions>("UsageSemantics"), At<ToggleOptions>("ExpectedLinuxEventTypes"),
                At<ToggleOptions>("ExpectedLinuxCodes"));
        Compare("gamepad_options", GamepadOptions.Init(), At<GamepadOptions>("Axes"));
        Compare("touchscreen_options", TouchscreenOptions.Init());
        Compare("touchpad_options", TouchpadOptions.Init());
        Compare("pen_options", PenOptions.Init(), At<PenOptions>("BarrelUsages"));
        Compare("digitizer_options", DigitizerOptions.Init(), At<DigitizerOptions>("BarrelUsages"));
        Compare("raw_options", RawOptions.Init());

        var gamepad = GamepadOptions.Init();
        for (var i = 0; i < (int)gamepad.AxisCount; i++)
        {
            var axis = Marshal.PtrToStructure<GamepadAxisOption>(gamepad.Axes + i * Marshal.SizeOf<GamepadAxisOption>());
            var seen = $"{(int)axis.Role},{axis.UsagePage},{axis.Usage},{axis.Value.LogicalMinimum},{axis.Value.LogicalMaximum},"
                       + $"{axis.Value.BitWidth},{axis.NeutralValue},{Marshal.PtrToStringUTF8(axis.ExpectedLinuxCode)},"
                       + Marshal.PtrToStringUTF8(axis.ExpectedAndroidAxis);
            if (lines[$"gamepad_axis{i}"] != seen)
                failures.Add($"gamepad_axis{i}: {seen} != {lines[$"gamepad_axis{i}"]}");
        }
        var pen = PenOptions.Init();
        if (lines["pen_barrel"] != $"{pen.BarrelUsageCount},{Marshal.ReadInt16(pen.BarrelUsages)}")
            failures.Add("pen_barrel differs");
        var strings = AccessoryOptions.Init().Strings;
        if (lines["accessory"] != $"{Marshal.PtrToStringUTF8(strings.Manufacturer)},{Marshal.PtrToStringUTF8(strings.Model)}")
            failures.Add("accessory strings differ");
        return failures;
    }
}
