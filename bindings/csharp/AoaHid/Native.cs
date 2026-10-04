// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
// Mirrors the complete stable native ABI without option initializers or native
// library deployment policy.
using System.Runtime.InteropServices;

namespace AoaHid;

public enum Result : int
{
    Ok = 0,
    Parameter = 1,
    UnsetField = 2,
    Unsupported = 3,
    NotAoa = 4,
    Access = 6,
    Busy = 7,
    NoDevice = 8,
    Stall = 9,
    Timeout = 10,
    ShortTransfer = 11,
    DescriptorRejected = 12,
    Io = 13,
    Overflow = 14,
    ClosePending = 15,
    Internal = 16,
}

public enum EventMode : int { CallerPoll = 1, InternalThread = 2 }
public enum ChannelReadMode : int { Stream = 0, Request = 1 }
public enum ClaimPolicy : int { None = 1, Explicit = 2 }
public enum LogLevel : int { Disabled = 1, Error = 2, Info = 3, Trace = 4 }
public enum PenMode : int { DirectScreen = 1, IndirectTablet = 2 }
public enum DpadRepresentation : int { None = 1, Hat = 2, Buttons = 3 }

public enum ProfileKind : int
{
    Keyboard = 1, Mouse = 2, Toggle = 3, Gamepad = 4,
    Touchscreen = 5, Pen = 6, Battery = 7, Raw = 8,
    Touchpad = 9,
}

public enum AndroidStatus : int
{
    PortableCandidate = 1, Conditional = 2, CustomSystemOnly = 3,
    Unsupported = 4, Unknown = 5,
}

public enum AxisRole : int
{
    X = 1, Y = 2, Z = 3, Rx = 4, Ry = 5, Rz = 6, Slider = 7,
    SimulationAccelerator = 8, SimulationBrake = 9,
    SimulationSteering = 10, Dial = 11, Wheel = 12,
    SimulationRudder = 13, SimulationThrottle = 14,
}

public enum UsageSemantic : int
{
    SelectorBitmap = 1, OnOffToggle = 2, OnOffMaintained = 3,
    Momentary = 4, OneShot = 5, Retrigger = 6, OnOffPair = 7,
    Linear = 8, DynamicValue = 9, NamedArray = 10,
}

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate void LogSink(nint user, LogLevel level, nint message);

[StructLayout(LayoutKind.Sequential)]
public struct ErrorDetail
{
    public int Code;
    public nint Field;
    public nint Reason;
    public int LibusbStatus;
    public int AoaRequest;
    public ushort HidId;
    public ushort ReportId;
    public uint Offset;
    public uint Length;
}

[StructLayout(LayoutKind.Sequential)]
public struct ContextOptions
{
    public uint StructSize;
    public uint Reserved;
    public EventMode EventMode;
    public LogLevel LogLevel;
    public nint LogSink;
    public nint LogUser;
}

[StructLayout(LayoutKind.Sequential)]
public struct DeviceInfo
{
    public byte BusNumber;
    public byte DeviceAddress;
    public nint PortPath;
    public nuint PortPathLength;
    public ushort VendorId;
    public ushort ProductId;
    public ushort ProtocolVersion;
    public nint Serial;
    public nint Product;
}

[StructLayout(LayoutKind.Sequential)]
public struct AoaStrings
{
    // AOA request-52 strings. AccessoryStart requires Manufacturer and Model.
    public nint Manufacturer;
    public nint Model;
    public nint Description;
    public nint Version;
    public nint Uri;
    public nint Serial;
}

[StructLayout(LayoutKind.Sequential)]
public struct DeviceOptions
{
    public uint StructSize;
    public uint Reserved;
    public uint ControlTimeoutMs;
    public uint SendTimeoutMs;
    public uint DescriptorFragmentBytes;
    public uint TransferPoolSlots;
    public uint MaximumReportBytes;
    public uint CloseDrainTimeoutMs;
    public uint AoaDescriptorWirePolicyBytes;
    public uint LinuxDescriptorPolicyBytes;
    public uint LinuxHidFieldsPerReportPolicy;
    public uint LinuxHidGlobalStackDepthPolicy;
    public ulong LinuxHidUsagesPolicy;
    public ulong LinuxHidReportDataBitsPolicy;
    public uint LinuxHidReportSizeBitsPolicy;
    public uint TargetEp0DataPolicyBytes;
    public uint HostControlBufferPolicyBytes;
    public ClaimPolicy InterfaceClaimPolicy;
    public int InterfaceNumber;
    public uint ValidateReports;
}

[StructLayout(LayoutKind.Sequential)]
public struct AccessoryOptions
{
    public uint StructSize;
    public uint Reserved;
    public AoaStrings Strings;
    public uint ControlTimeoutMs;
}

[StructLayout(LayoutKind.Sequential)]
public struct ChannelOptions
{
    public uint StructSize;
    public uint Reserved;
    public byte InterfaceClass;
    public byte InterfaceSubclass;
    public byte InterfaceProtocol;
    public byte Reserved8;
    public uint InTransfers;
    public uint OutTransfers;
    public uint TransferBytes;
    public uint ZeroLengthTermination;
    public ChannelReadMode ReadMode;
}

[StructLayout(LayoutKind.Sequential)]
public struct NodeOptions
{
    public uint StructSize;
    public uint Reserved;
    public uint HasReservedSlots;
    public uint ReservedSlots;
}

[StructLayout(LayoutKind.Sequential)]
public struct PhysicalProperties
{
    public uint Enabled;
    public int Minimum;
    public int Maximum;
    public int UnitExponent;
    public uint Unit;
}

[StructLayout(LayoutKind.Sequential)]
public struct IntegerField
{
    public int LogicalMinimum;
    public int LogicalMaximum;
    public uint BitWidth;
    public PhysicalProperties Physical;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct ReportId
{
    public uint Enabled;
    public byte Value;
    public fixed byte Reserved8[3];
}

[StructLayout(LayoutKind.Sequential)]
public struct KeyboardOptions
{
    public uint StructSize;
    public uint Reserved;
    public ReportId ReportId;
    public ushort UsageMinimum;
    public ushort UsageMaximum;
}

[StructLayout(LayoutKind.Sequential)]
public struct MouseOptions
{
    public uint StructSize;
    public uint Reserved;
    public ReportId ReportId;
    public uint ButtonCount;
    public IntegerField X;
    public IntegerField Y;
    public uint EnableWheel;
    public IntegerField Wheel;
    public uint EnablePan;
    public IntegerField Pan;
}

[StructLayout(LayoutKind.Sequential)]
public struct ToggleOptions
{
    public uint StructSize;
    public uint Reserved;
    public ReportId ReportId;
    public ushort ApplicationPage;
    public ushort ApplicationUsage;
    public ushort FieldPage;
    public ushort Reserved16;
    public nint AllowedUsages;
    public nuint AllowedUsageCount;
    public nint UsageSemantics;
    public nint ExpectedLinuxEventTypes;
    public nint ExpectedLinuxCodes;
}

[StructLayout(LayoutKind.Sequential)]
public struct GamepadAxis
{
    public AxisRole Role;
    public ushort UsagePage;
    public ushort Usage;
    public IntegerField Value;
    public int NeutralValue;
    public nint ExpectedLinuxCode;
    public nint ExpectedAndroidAxis;
}

[StructLayout(LayoutKind.Sequential)]
public struct GamepadOptions
{
    public uint StructSize;
    public uint Reserved;
    public ReportId ReportId;
    public nint Axes;
    public nuint AxisCount;
    public uint ButtonCount;
    public ushort ButtonUsageMinimum;
    public DpadRepresentation DpadRepresentation;
    public int HatLogicalMinimum;
    public int HatLogicalMaximum;
    public uint HatBitWidth;
}

[StructLayout(LayoutKind.Sequential)]
public struct TouchscreenOptions
{
    public uint StructSize;
    public uint Reserved;
    public ReportId ReportId;
    public uint MaximumContacts;
    public uint ContactsPerReport;
    public IntegerField ContactIdentifier;
    public IntegerField X;
    public IntegerField Y;
    public IntegerField ContactCount;
    public uint EnablePressure;
    public IntegerField Pressure;
    public uint EnableWidth;
    public IntegerField Width;
    public uint EnableHeight;
    public IntegerField Height;
    public uint EnableAzimuth;
    public IntegerField Azimuth;
    public uint EnableScanTime;
    public IntegerField ScanTime;
    public uint ScanTimeUnit100us;
    public uint EnableContactCountMaximumFeatureDeclaration;
    public uint EnableMultiPacketFrames;
}

[StructLayout(LayoutKind.Sequential)]
public struct TouchpadOptions
{
    public uint StructSize;
    public uint Reserved;
    public ReportId ReportId;
    public uint MaximumContacts;
    public uint ContactsPerReport;
    public IntegerField ContactIdentifier;
    public IntegerField X;
    public IntegerField Y;
    public IntegerField ContactCount;
    public uint EnablePressure;
    public IntegerField Pressure;
    public uint EnableWidth;
    public IntegerField Width;
    public uint EnableHeight;
    public IntegerField Height;
    public uint EnableAzimuth;
    public IntegerField Azimuth;
    public uint EnableScanTime;
    public IntegerField ScanTime;
    public uint ScanTimeUnit100us;
    public uint EnableContactCountMaximumFeatureDeclaration;
    public uint EnableMultiPacketFrames;
    public uint ButtonCount;
}

[StructLayout(LayoutKind.Sequential)]
public struct PenOptions
{
    public uint StructSize;
    public uint Reserved;
    public ReportId ReportId;
    public PenMode Mode;
    public IntegerField X;
    public IntegerField Y;
    public uint EnablePressure;
    public IntegerField Pressure;
    public uint EnableTilt;
    public IntegerField TiltX;
    public IntegerField TiltY;
    public uint EnableTwistTargetSpecific;
    public IntegerField Twist;
    public nint BarrelUsages;
    public nuint BarrelUsageCount;
    public uint EnableEraser;
    public uint EnableHover;
}

[StructLayout(LayoutKind.Sequential)]
public struct BatteryOptions
{
    public uint StructSize;
    public uint Reserved;
    public ReportId ReportId;
    public IntegerField Strength;
    public uint EnableUnknownNullState;
}

[StructLayout(LayoutKind.Sequential)]
public struct RawReport
{
    public byte ReportId;
    public byte HasReportId;
    public ushort Reserved16;
    public uint WireLength;
}

[StructLayout(LayoutKind.Sequential)]
public struct RawOptions
{
    public uint StructSize;
    public uint Reserved;
    public nint Descriptor;
    public nuint DescriptorLength;
    public nint Reports;
    public nuint ReportCount;
    public uint AcknowledgesNoAndroidSupport;
    public uint RequiresOutput;
    public uint RequiresFeatureResponse;
}

[StructLayout(LayoutKind.Sequential)]
public struct ReportCapability
{
    public byte ReportId;
    public byte HasReportId;
    public ushort Reserved16;
    public uint WireLength;
}

[StructLayout(LayoutKind.Sequential)]
public struct CapabilityManifest
{
    public uint StructSize;
    public uint Reserved;
    public uint InputSupported;
    public uint OutputSupported;
    public uint FeatureTransportSupported;
    public AndroidStatus AndroidStatus;
    public ProfileKind ProfileKind;
    public nint Reports;
    public nuint ReportCount;
    public nuint DescriptorBytes;
}

[StructLayout(LayoutKind.Sequential)]
public struct TouchContact
{
    public uint ContactId;
    public int X;
    public int Y;
    public int Pressure;
    public int Width;
    public int Height;
    public int Azimuth;
}

[StructLayout(LayoutKind.Sequential)]
public struct TouchExtra
{
    public int Pressure;
    public int Width;
    public int Height;
    public int Azimuth;
}

[StructLayout(LayoutKind.Sequential)]
public struct PenSample
{
    public uint InRange;
    public uint Tip;
    public uint Eraser;
    public uint BarrelButtons;
    public int X;
    public int Y;
    public int Pressure;
    public int TiltX;
    public int TiltY;
    public int Twist;
}

public static class Native
{
    private const string Library = "aoahid";
    private const CallingConvention Call = CallingConvention.Cdecl;
    public const uint AOAHID_VERSION_MAJOR = 4;
    public const uint AOAHID_VERSION_MINOR = 1;
    public const uint AOAHID_VERSION_PATCH = 0;

    public static readonly IReadOnlyDictionary<string, int> AbiConstants =
        new Dictionary<string, int>
        {
            ["AOAHID_OK"] = 0,
            ["AOAHID_ERR_PARAM"] = 1,
            ["AOAHID_ERR_UNSET_FIELD"] = 2,
            ["AOAHID_ERR_UNSUPPORTED"] = 3,
            ["AOAHID_ERR_NOT_AOA"] = 4,
            ["AOAHID_ERR_ACCESS"] = 6,
            ["AOAHID_ERR_BUSY"] = 7,
            ["AOAHID_ERR_NO_DEVICE"] = 8,
            ["AOAHID_ERR_STALL"] = 9,
            ["AOAHID_ERR_TIMEOUT"] = 10,
            ["AOAHID_ERR_SHORT_TRANSFER"] = 11,
            ["AOAHID_ERR_DESCRIPTOR_REJECTED"] = 12,
            ["AOAHID_ERR_IO"] = 13,
            ["AOAHID_ERR_OVERFLOW"] = 14,
            ["AOAHID_CLOSE_PENDING"] = 15,
            ["AOAHID_ERR_INTERNAL"] = 16,
            ["AOAHID_EVENT_CALLER_POLL"] = 1,
            ["AOAHID_EVENT_INTERNAL_THREAD"] = 2,
            ["AOAHID_CHANNEL_READ_STREAM"] = 0,
            ["AOAHID_CHANNEL_READ_REQUEST"] = 1,
            ["AOAHID_INTERFACE_CLAIM_NONE"] = 1,
            ["AOAHID_INTERFACE_CLAIM_EXPLICIT"] = 2,
            ["AOAHID_LOG_DISABLED"] = 1,
            ["AOAHID_LOG_ERROR"] = 2,
            ["AOAHID_LOG_INFO"] = 3,
            ["AOAHID_LOG_TRACE"] = 4,
            ["AOAHID_PROFILE_KEYBOARD"] = 1,
            ["AOAHID_PROFILE_MOUSE"] = 2,
            ["AOAHID_PROFILE_TOGGLE"] = 3,
            ["AOAHID_PROFILE_GAMEPAD"] = 4,
            ["AOAHID_PROFILE_TOUCHSCREEN"] = 5,
            ["AOAHID_PROFILE_PEN"] = 6,
            ["AOAHID_PROFILE_BATTERY"] = 7,
            ["AOAHID_PROFILE_RAW"] = 8,
            ["AOAHID_PROFILE_TOUCHPAD"] = 9,
            ["AOAHID_ANDROID_PORTABLE_CANDIDATE"] = 1,
            ["AOAHID_ANDROID_CONDITIONAL"] = 2,
            ["AOAHID_ANDROID_CUSTOM_SYSTEM_ONLY"] = 3,
            ["AOAHID_ANDROID_UNSUPPORTED"] = 4,
            ["AOAHID_ANDROID_UNKNOWN"] = 5,
            ["AOAHID_PEN_DIRECT_SCREEN"] = 1,
            ["AOAHID_PEN_INDIRECT_TABLET"] = 2,
            ["AOAHID_AXIS_X"] = 1,
            ["AOAHID_AXIS_Y"] = 2,
            ["AOAHID_AXIS_Z"] = 3,
            ["AOAHID_AXIS_RX"] = 4,
            ["AOAHID_AXIS_RY"] = 5,
            ["AOAHID_AXIS_RZ"] = 6,
            ["AOAHID_AXIS_SLIDER"] = 7,
            ["AOAHID_AXIS_SIMULATION_ACCELERATOR"] = 8,
            ["AOAHID_AXIS_SIMULATION_BRAKE"] = 9,
            ["AOAHID_AXIS_SIMULATION_STEERING"] = 10,
            ["AOAHID_AXIS_DIAL"] = 11,
            ["AOAHID_AXIS_WHEEL"] = 12,
            ["AOAHID_AXIS_SIMULATION_RUDDER"] = 13,
            ["AOAHID_AXIS_SIMULATION_THROTTLE"] = 14,
            ["AOAHID_DPAD_NONE"] = 1,
            ["AOAHID_DPAD_HAT"] = 2,
            ["AOAHID_DPAD_BUTTONS"] = 3,
            ["AOAHID_USAGE_SELECTOR_BITMAP"] = 1,
            ["AOAHID_USAGE_ON_OFF_TOGGLE"] = 2,
            ["AOAHID_USAGE_ON_OFF_MAINTAINED"] = 3,
            ["AOAHID_USAGE_MOMENTARY"] = 4,
            ["AOAHID_USAGE_ONE_SHOT"] = 5,
            ["AOAHID_USAGE_RETRIGGER"] = 6,
            ["AOAHID_USAGE_ON_OFF_PAIR"] = 7,
            ["AOAHID_USAGE_LINEAR"] = 8,
            ["AOAHID_USAGE_DYNAMIC_VALUE"] = 9,
            ["AOAHID_USAGE_NAMED_ARRAY"] = 10,
        };

    public static readonly IReadOnlyDictionary<string, Type> AbiStructs =
        new Dictionary<string, Type>
        {
            ["aoahid_error_detail"] = typeof(ErrorDetail),
            ["aoahid_context_options"] = typeof(ContextOptions),
            ["aoahid_device_info"] = typeof(DeviceInfo),
            ["aoahid_aoa_strings"] = typeof(AoaStrings),
            ["aoahid_device_options"] = typeof(DeviceOptions),
            ["aoahid_accessory_options"] = typeof(AccessoryOptions),
            ["aoahid_channel_options"] = typeof(ChannelOptions),
            ["aoahid_node_options"] = typeof(NodeOptions),
            ["aoahid_physical_properties"] = typeof(PhysicalProperties),
            ["aoahid_integer_field"] = typeof(IntegerField),
            ["aoahid_report_id_option"] = typeof(ReportId),
            ["aoahid_keyboard_options"] = typeof(KeyboardOptions),
            ["aoahid_mouse_options"] = typeof(MouseOptions),
            ["aoahid_toggle_options"] = typeof(ToggleOptions),
            ["aoahid_gamepad_axis"] = typeof(GamepadAxis),
            ["aoahid_gamepad_options"] = typeof(GamepadOptions),
            ["aoahid_touchscreen_options"] = typeof(TouchscreenOptions),
            ["aoahid_touchpad_options"] = typeof(TouchpadOptions),
            ["aoahid_pen_options"] = typeof(PenOptions),
            ["aoahid_battery_options"] = typeof(BatteryOptions),
            ["aoahid_raw_report"] = typeof(RawReport),
            ["aoahid_raw_options"] = typeof(RawOptions),
            ["aoahid_report_capability"] = typeof(ReportCapability),
            ["aoahid_capability_manifest"] = typeof(CapabilityManifest),
            ["aoahid_touch_contact"] = typeof(TouchContact),
            ["aoahid_touch_extra"] = typeof(TouchExtra),
            ["aoahid_pen_sample"] = typeof(PenSample),
        };

    [DllImport(Library, EntryPoint = "aoahid_last_error", ExactSpelling = true, CallingConvention = Call)]
    public static extern nint LastError();
    [DllImport(Library, EntryPoint = "aoahid_result_name", ExactSpelling = true, CallingConvention = Call)]
    public static extern nint ResultName(Result result);
    [DllImport(Library, EntryPoint = "aoahid_version", ExactSpelling = true, CallingConvention = Call)]
    public static extern uint Version();
    [DllImport(Library, EntryPoint = "aoahid_context_create", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result ContextCreate(in ContextOptions options, out nint context);
    [DllImport(Library, EntryPoint = "aoahid_context_poll", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result ContextPoll(nint context, uint timeoutMs);
    [DllImport(Library, EntryPoint = "aoahid_context_destroy", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result ContextDestroy(nint context);
    [DllImport(Library, EntryPoint = "aoahid_context_destroy_blocking", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result ContextDestroyBlocking(nint context, uint timeoutMs);
    [DllImport(Library, EntryPoint = "aoahid_discover", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result Discover(nint context, uint controlTimeoutMs, out nint discovery);
    [DllImport(Library, EntryPoint = "aoahid_discovery_count", ExactSpelling = true, CallingConvention = Call)]
    public static extern nuint DiscoveryCount(nint discovery);
    [DllImport(Library, EntryPoint = "aoahid_discovery_get", ExactSpelling = true, CallingConvention = Call)]
    public static extern nint DiscoveryGet(nint discovery, nuint index);
    [DllImport(Library, EntryPoint = "aoahid_discovery_destroy", ExactSpelling = true, CallingConvention = Call)]
    public static extern void DiscoveryDestroy(nint discovery);
    [DllImport(Library, EntryPoint = "aoahid_accessory_start", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result AccessoryStart(nint context, in DeviceInfo selected, in AccessoryOptions options);
    [DllImport(Library, EntryPoint = "aoahid_device_open", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result DeviceOpen(nint context, in DeviceInfo selected, in DeviceOptions options, out nint device);
    [DllImport(Library, EntryPoint = "aoahid_device_close", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result DeviceClose(nint device);
    [DllImport(Library, EntryPoint = "aoahid_device_latched_error", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result DeviceLatchedError(nint device);

    [DllImport(Library, EntryPoint = "aoahid_channel_open", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result ChannelOpen(nint device, in ChannelOptions options, out nint channel);
    [DllImport(Library, EntryPoint = "aoahid_channel_close", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result ChannelClose(nint channel);
    [DllImport(Library, EntryPoint = "aoahid_channel_write", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result ChannelWrite(nint channel, nint data, nuint length, out nuint written, uint timeoutMs);
    [DllImport(Library, EntryPoint = "aoahid_channel_read", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result ChannelRead(nint channel, nint buffer, nuint capacity, out nuint received, uint timeoutMs);

    [DllImport(Library, EntryPoint = "aoahid_spec_create_keyboard", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result SpecCreateKeyboard(in KeyboardOptions options, out nint spec);
    [DllImport(Library, EntryPoint = "aoahid_spec_create_mouse", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result SpecCreateMouse(in MouseOptions options, out nint spec);
    [DllImport(Library, EntryPoint = "aoahid_spec_create_toggle", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result SpecCreateToggle(in ToggleOptions options, out nint spec);
    [DllImport(Library, EntryPoint = "aoahid_spec_create_gamepad", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result SpecCreateGamepad(in GamepadOptions options, out nint spec);
    [DllImport(Library, EntryPoint = "aoahid_spec_create_touchscreen", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result SpecCreateTouchscreen(in TouchscreenOptions options, out nint spec);
    [DllImport(Library, EntryPoint = "aoahid_spec_create_touchpad", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result SpecCreateTouchpad(in TouchpadOptions options, out nint spec);
    [DllImport(Library, EntryPoint = "aoahid_spec_create_pen", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result SpecCreatePen(in PenOptions options, out nint spec);
    [DllImport(Library, EntryPoint = "aoahid_spec_create_battery", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result SpecCreateBattery(in BatteryOptions options, out nint spec);
    [DllImport(Library, EntryPoint = "aoahid_spec_create_raw", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result SpecCreateRaw(in RawOptions options, out nint spec);
    [DllImport(Library, EntryPoint = "aoahid_spec_retain", ExactSpelling = true, CallingConvention = Call)]
    public static extern void SpecRetain(nint spec);
    [DllImport(Library, EntryPoint = "aoahid_spec_release", ExactSpelling = true, CallingConvention = Call)]
    public static extern void SpecRelease(nint spec);
    [DllImport(Library, EntryPoint = "aoahid_spec_descriptor", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result SpecDescriptor(nint spec, out nint bytes, out nuint length);
    [DllImport(Library, EntryPoint = "aoahid_spec_manifest", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result SpecManifest(nint spec, ref CapabilityManifest manifest);

    [DllImport(Library, EntryPoint = "aoahid_node_open", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result NodeOpen(nint device, nint spec, in NodeOptions options, out nint node);
    [DllImport(Library, EntryPoint = "aoahid_node_close", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result NodeClose(nint node);
    [DllImport(Library, EntryPoint = "aoahid_node_hid_id", ExactSpelling = true, CallingConvention = Call)]
    public static extern ushort NodeHidId(nint node);
    [DllImport(Library, EntryPoint = "aoahid_node_manifest", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result NodeManifest(nint node, ref CapabilityManifest manifest);
    [DllImport(Library, EntryPoint = "aoahid_node_submit", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result NodeSubmit(nint node);
    [DllImport(Library, EntryPoint = "aoahid_node_submit_blocking", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result NodeSubmitBlocking(nint node, uint deadlineMs);

    [DllImport(Library, EntryPoint = "aoahid_kbd", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result Kbd(nint node, ushort usage, uint down);
    [DllImport(Library, EntryPoint = "aoahid_mouse_move", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result MouseMove(nint node, int dx, int dy);
    [DllImport(Library, EntryPoint = "aoahid_mouse_scroll", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result MouseScroll(nint node, int wheel, int pan);
    [DllImport(Library, EntryPoint = "aoahid_mouse_button", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result MouseButton(nint node, uint button, uint pressed);
    [DllImport(Library, EntryPoint = "aoahid_toggle", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result Toggle(nint node, ushort usage, uint down);
    [DllImport(Library, EntryPoint = "aoahid_gamepad_button", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result GamepadButton(nint node, uint button, uint pressed);
    [DllImport(Library, EntryPoint = "aoahid_gamepad_set_axis", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result GamepadSetAxis(nint node, nuint axisIndex, int value);
    [DllImport(Library, EntryPoint = "aoahid_dpad", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result Dpad(nint node, uint up, uint down, uint right, uint left);
    // Pass default(TouchExtra) for "no extra data"; this native function treats a null
    // pointer and an all-zero struct identically, so no separate nint overload is needed.
    [DllImport(Library, EntryPoint = "aoahid_touch", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result Touch(nint node, uint contactId, uint down, int x, int y, in TouchExtra extra);
    [DllImport(Library, EntryPoint = "aoahid_touchpad_button", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result TouchpadButton(nint node, uint button, uint pressed);
    [DllImport(Library, EntryPoint = "aoahid_pen_update", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result PenUpdate(nint node, in PenSample sample);
    [DllImport(Library, EntryPoint = "aoahid_pen_depart", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result PenDepart(nint node);
    [DllImport(Library, EntryPoint = "aoahid_battery_update", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result BatteryUpdate(nint node, uint hasValue, int strength);
    [DllImport(Library, EntryPoint = "aoahid_raw_submit", ExactSpelling = true, CallingConvention = Call)]
    public static extern Result RawSubmit(nint node, nint report, nuint length);
}
