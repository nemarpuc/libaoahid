// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
// The values of the C aoahid_*_init functions of aoahid.h, for the structures whose initializer
// fills values (HAND_INITIAL in tools/bindings/generate.py). AoaHid.Tests/InitialCheck.cs compares
// each with what the compiled C initializer leaves, so the two cannot drift apart. The few native
// blocks the pointer fields point at are allocated once and live for the life of the process.
using System.Runtime.InteropServices;

namespace AoaHid;

/// <summary>Helpers for the Init() values.</summary>
public static class Initial
{
    /// <summary>The C aoahid_integer_field_make: no physical properties.</summary>
    public static IntegerField Field(int logicalMinimum, int logicalMaximum, uint bitWidth) =>
        new() { LogicalMinimum = logicalMinimum, LogicalMaximum = logicalMaximum, BitWidth = bitWidth };

    internal static uint SizeOf<T>() where T : struct => (uint)Marshal.SizeOf<T>();

    internal static readonly nint LibAoaHid = Marshal.StringToCoTaskMemUTF8("libaoahid");

    private static GamepadAxisOption Axis(AxisRole role, ushort usage, string code, string android) => new()
    {
        Role = role, UsagePage = 0x01, Usage = usage, Value = Field(-127, 127, 8), NeutralValue = 0,
        ExpectedLinuxCode = Marshal.StringToCoTaskMemUTF8(code),
        ExpectedAndroidAxis = Marshal.StringToCoTaskMemUTF8(android),
    };

    internal static readonly nint GamepadAxes = MakeGamepadAxes();

    private static nint MakeGamepadAxes()
    {
        var axes = new[]
        {
            Axis(AxisRole.X, 0x30, "ABS_X", "AXIS_X"), Axis(AxisRole.Y, 0x31, "ABS_Y", "AXIS_Y"),
            Axis(AxisRole.Z, 0x32, "ABS_Z", "AXIS_Z"), Axis(AxisRole.Rz, 0x35, "ABS_RZ", "AXIS_RZ"),
        };
        var size = Marshal.SizeOf<GamepadAxisOption>();
        var block = Marshal.AllocHGlobal(size * axes.Length);
        for (var i = 0; i < axes.Length; i++)
            Marshal.StructureToPtr(axes[i], block + i * size, false);
        return block;
    }

    internal static readonly nint BarrelUsages = MakeBarrelUsages();

    private static nint MakeBarrelUsages()
    {
        var block = Marshal.AllocHGlobal(sizeof(ushort));
        Marshal.WriteInt16(block, 0x44);
        return block;
    }
}

public partial struct ContextOptions
{
    /// <summary>An internal event thread and no logging, as the C aoahid_context_options_init.</summary>
    public static ContextOptions Init() => new()
    {
        StructSize = Initial.SizeOf<ContextOptions>(), EventMode = EventMode.InternalThread, LogLevel = LogLevel.Disabled,
    };
}

public partial struct DeviceOptions
{
    /// <summary>4096-byte buffers, 4 transfer slots, 1024-byte reports, no interface claim and the Linux HID limits of docs/LIMITS.md.</summary>
    public static DeviceOptions Init() => new()
    {
        StructSize = Initial.SizeOf<DeviceOptions>(),
        DescriptorFragmentBytes = 4096, TransferPoolSlots = 4, MaximumReportBytes = 1024,
        AoaDescriptorWirePolicyBytes = 4096,
        LinuxHid = new LinuxHidPolicy
        {
            DescriptorBytes = 4096, FieldsPerReport = 256, GlobalStackDepth = 4, ReportSizeBits = 256,
            Usages = 12288, ReportDataBits = 65528,
        },
        TargetEp0DataPolicyBytes = 4096, HostControlBufferPolicyBytes = 4096,
        InterfaceClaimPolicy = ClaimPolicy.None, InterfaceNumber = -1,
    };
}

public partial struct AccessoryOptions
{
    /// <summary>Manufacturer and model "libaoahid"; set your own when you ship an accessory.</summary>
    public static AccessoryOptions Init() => new()
    {
        StructSize = Initial.SizeOf<AccessoryOptions>(),
        Strings = new AoaStrings { Manufacturer = Initial.LibAoaHid, Model = Initial.LibAoaHid },
    };
}

public partial struct ChannelOptions
{
    /// <summary>The ADB interface, read on request, 1 MiB transfers, two OUT transfers, zero-length packets.</summary>
    public static ChannelOptions Init() => new()
    {
        StructSize = Initial.SizeOf<ChannelOptions>(),
        InterfaceClass = 0xFF, InterfaceSubclass = 0x42, InterfaceProtocol = 0x01,
        OutTransfers = 2, TransferBytes = 1_048_576, ZeroLengthTermination = 1, ReadMode = ChannelReadMode.Request,
    };
}

public partial struct KeyboardOptions
{
    /// <summary>Every key from Keyboard a (0x04) to Keypad Hexadecimal (0xDD).</summary>
    public static KeyboardOptions Init() => new()
    {
        StructSize = Initial.SizeOf<KeyboardOptions>(), UsageMinimum = 0x04, UsageMaximum = 0xDD,
    };
}

public partial struct MouseOptions
{
    /// <summary>Five buttons, 16-bit X and Y, and an 8-bit wheel and pan.</summary>
    public static MouseOptions Init() => new()
    {
        StructSize = Initial.SizeOf<MouseOptions>(), ButtonCount = 5,
        X = Initial.Field(-32767, 32767, 16), Y = Initial.Field(-32767, 32767, 16),
        EnableWheel = 1, Wheel = Initial.Field(-127, 127, 8), EnablePan = 1, Pan = Initial.Field(-127, 127, 8),
    };
}

public partial struct ToggleOptions
{
    /// <summary>A Consumer Control collection; the keys themselves are the caller's to list.</summary>
    public static ToggleOptions Init() => new()
    {
        StructSize = Initial.SizeOf<ToggleOptions>(), ApplicationPage = 0x0C, ApplicationUsage = 0x01, FieldPage = 0x0C,
    };
}

public partial struct GamepadOptions
{
    /// <summary>Two sticks (X/Y and Z/Rz), twelve buttons from Button 1 and an eight-way Hat.</summary>
    public static GamepadOptions Init() => new()
    {
        StructSize = Initial.SizeOf<GamepadOptions>(), Axes = Initial.GamepadAxes, AxisCount = 4,
        ButtonCount = 12, ButtonUsageMinimum = 1, DpadRepresentation = DpadRepresentation.Hat,
        HatLogicalMinimum = 0, HatLogicalMaximum = 7, HatBitWidth = 4,
    };
}

public partial struct TouchscreenOptions
{
    /// <summary>Ten contacts in one report, X and Y from 0 to 32767, and no optional field.</summary>
    public static TouchscreenOptions Init() => new()
    {
        StructSize = Initial.SizeOf<TouchscreenOptions>(), MaximumContacts = 10, ContactsPerReport = 10,
        ContactId = Initial.Field(0, 15, 4), X = Initial.Field(0, 32767, 16), Y = Initial.Field(0, 32767, 16),
        ContactCount = Initial.Field(0, 10, 4),
    };
}

public partial struct TouchpadOptions
{
    /// <summary>Five contacts in one report, X and Y from 0 to 32767, no optional field, one click button.</summary>
    public static TouchpadOptions Init() => new()
    {
        StructSize = Initial.SizeOf<TouchpadOptions>(), MaximumContacts = 5, ContactsPerReport = 5,
        ContactId = Initial.Field(0, 15, 4), X = Initial.Field(0, 32767, 16), Y = Initial.Field(0, 32767, 16),
        ContactCount = Initial.Field(0, 5, 4), ButtonCount = 1,
    };
}

public partial struct PenOptions
{
    /// <summary>X and Y from 0 to 32767, 12-bit pressure, one Barrel Switch, and hover.</summary>
    public static PenOptions Init() => new()
    {
        StructSize = Initial.SizeOf<PenOptions>(), X = Initial.Field(0, 32767, 16), Y = Initial.Field(0, 32767, 16),
        EnablePressure = 1, Pressure = Initial.Field(0, 4095, 12), BarrelUsages = Initial.BarrelUsages,
        BarrelUsageCount = 1, EnableHover = 1,
    };
}

public partial struct DigitizerOptions
{
    /// <summary>The same values as PenOptions.Init().</summary>
    public static DigitizerOptions Init() => new()
    {
        StructSize = Initial.SizeOf<DigitizerOptions>(), X = Initial.Field(0, 32767, 16), Y = Initial.Field(0, 32767, 16),
        EnablePressure = 1, Pressure = Initial.Field(0, 4095, 12), BarrelUsages = Initial.BarrelUsages,
        BarrelUsageCount = 1, EnableHover = 1,
    };
}
