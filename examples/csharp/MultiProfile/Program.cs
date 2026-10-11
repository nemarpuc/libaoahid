// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
// Exercises explicit discovery, profile, reconnection, and shutdown policies
// through P/Invoke. The binding provides no option values or ownership policy.
// docs/EXAMPLES.md explains the Usage and protocol choices.
using System.Runtime.InteropServices;
using AoaHid;

internal static class Program
{
    // Every value in this block is this caller's reviewed example policy or
    // input. None is selected by libaoahid; docs/EXAMPLES.md records each one.
    private const uint DescriptorPolicyBytes = 4096;
    private const uint HostReportPolicyBytes = 4088;
    private const uint PoolSlots = 8;
    private const uint ReservedSlotsPerNode = 1;
    private const uint CloseDrainTimeoutMs = 1000;
    private const ushort KeyboardAUsage = 0x04; // HUT 1.7 section 10.
    private const ushort KeyboardApplicationUsage = 0x65; // HUT 1.7 section 10.
    private const uint PointerButtonCount = 3;
    private const int RelativeMinimum = -127;
    private const int RelativeMaximum = 127;
    private const uint RelativeBits = 8;
    private const uint TouchContactId = 1;
    private const int TouchContactIdMaximum = 15;
    private const uint TouchContactIdBits = 4;
    private const int TouchCoordinateMaximum = 32767;
    private const uint TouchCoordinateBits = 16;
    private const uint TouchContactCountBits = 1;
    private const int ExampleTouchX = 1000;
    private const int ExampleTouchY = 2000;
    private const int ExampleMouseDx = 30;
    private const int ExampleMouseDy = -20;

    private sealed record Locator(byte BusNumber, byte[] PortPath, string Serial);

    private sealed record Specs(nint Keyboard, nint Mouse, nint Touchscreen);

    private sealed class Session(Locator locator)
    {
        public Locator Locator { get; } = locator;
        public nint Device { get; set; }
        public nint Keyboard { get; set; }
        public nint Mouse { get; set; }
        public nint Touchscreen { get; set; }
    }

    private sealed class Worker(Locator locator, uint timeoutMs)
    {
        public int ExitCode { get; private set; }

        public void Run()
        {
            ExitCode = RunWorker(locator, timeoutMs);
        }
    }

    private static int Main(string[] args)
    {
        if (args.Length != 2 || !uint.TryParse(args[0], out var timeoutMs) || timeoutMs == 0 ||
            (args[1] != "shared" && args[1] != "threaded"))
        {
            Console.Error.WriteLine("usage: MultiProfile CONTROL_TIMEOUT_MS shared|threaded");
            return 2;
        }
        return args[1] == "shared" ? RunShared(timeoutMs) : RunThreaded(timeoutMs);
    }

    private static ContextOptions ContextConfiguration() => new()
    {
        StructSize = checked((uint)Marshal.SizeOf<ContextOptions>()),
        Reserved = 0,
        EventMode = EventMode.CallerPoll,
        LogLevel = LogLevel.Disabled,
        LogSink = nint.Zero,
        LogUser = nint.Zero,
    };

    private static DeviceOptions DeviceConfiguration(uint timeoutMs) => new()
    {
        StructSize = checked((uint)Marshal.SizeOf<DeviceOptions>()),
        Reserved = 0,
        ControlTimeoutMs = timeoutMs,
        SendTimeoutMs = timeoutMs,
        DescriptorFragmentBytes = 64, // Explicit policy; AOA fixes no fragment size.
        TransferPoolSlots = PoolSlots,
        MaximumReportBytes = HostReportPolicyBytes,
        CloseDrainTimeoutMs = CloseDrainTimeoutMs,
        ValidateReports = 1,
        AoaDescriptorWirePolicyBytes = DescriptorPolicyBytes,
        LinuxDescriptorPolicyBytes = DescriptorPolicyBytes,
        // These policies mirror Linux HID core limits
        // (include/linux/hid.h); check them against the target kernel.
        LinuxHidFieldsPerReportPolicy = 256,
        LinuxHidGlobalStackDepthPolicy = 4,
        LinuxHidUsagesPolicy = 12288,
        LinuxHidReportDataBitsPolicy = 65528,
        LinuxHidReportSizeBitsPolicy = 256,
        TargetEp0DataPolicyBytes = DescriptorPolicyBytes,
        HostControlBufferPolicyBytes = HostReportPolicyBytes,
        InterfaceClaimPolicy = ClaimPolicy.None,
        InterfaceNumber = -1, // No interface number accompanies the no-claim policy.
    };

    private static NodeOptions NodeConfiguration() => new()
    {
        StructSize = checked((uint)Marshal.SizeOf<NodeOptions>()),
        Reserved = 0,
        HasReservedSlots = 1,
        ReservedSlots = ReservedSlotsPerNode,
    };

    private static IntegerField Field(int minimum, int maximum, uint bits) => new()
    {
        LogicalMinimum = minimum,
        LogicalMaximum = maximum,
        BitWidth = bits,
        Physical = new PhysicalProperties
        {
            Enabled = 0,
            Minimum = 0,
            Maximum = 0,
            UnitExponent = 0,
            Unit = 0,
        },
    };

    private static Specs? CreateSpecs()
    {
        var keyboardOptions = new KeyboardOptions
        {
            StructSize = checked((uint)Marshal.SizeOf<KeyboardOptions>()),
            Reserved = 0,
            ReportId = new ReportId(), // Explicitly selects a no-Report-ID descriptor.
            UsageMinimum = KeyboardAUsage,
            UsageMaximum = KeyboardApplicationUsage,
        };
        var mouseOptions = new MouseOptions
        {
            StructSize = checked((uint)Marshal.SizeOf<MouseOptions>()),
            Reserved = 0,
            ReportId = new ReportId(),
            ButtonCount = PointerButtonCount,
            X = Field(RelativeMinimum, RelativeMaximum, RelativeBits),
            Y = Field(RelativeMinimum, RelativeMaximum, RelativeBits),
            EnableWheel = 1,
            Wheel = Field(RelativeMinimum, RelativeMaximum, RelativeBits),
            EnablePan = 0,
            Pan = Field(0, 0, 0),
        };
        var touchOptions = new TouchscreenOptions
        {
            StructSize = checked((uint)Marshal.SizeOf<TouchscreenOptions>()),
            Reserved = 0,
            ReportId = new ReportId(),
            MaximumContacts = 1,
            ContactsPerReport = 1,
            ContactIdentifier = Field(0, TouchContactIdMaximum, TouchContactIdBits),
            X = Field(0, TouchCoordinateMaximum, TouchCoordinateBits),
            Y = Field(0, TouchCoordinateMaximum, TouchCoordinateBits),
            ContactCount = Field(0, 1, TouchContactCountBits),
            EnablePressure = 0,
            Pressure = Field(0, 0, 0),
            EnableWidth = 0,
            Width = Field(0, 0, 0),
            EnableHeight = 0,
            Height = Field(0, 0, 0),
            EnableAzimuth = 0,
            Azimuth = Field(0, 0, 0),
            EnableScanTime = 0,
            ScanTime = Field(0, 0, 0),
            ScanTimeUnit100us = 0,
            EnableContactCountMaximumFeatureDeclaration = 0,
            EnableMultiPacketFrames = 0,
        };

        nint keyboard = nint.Zero;
        nint mouse = nint.Zero;
        nint touchscreen = nint.Zero;
        var status = Native.SpecCreateKeyboard(in keyboardOptions, out keyboard);
        if (status == Result.Ok)
            status = Native.SpecCreateMouse(in mouseOptions, out mouse);
        if (status == Result.Ok)
            status = Native.SpecCreateTouchscreen(in touchOptions, out touchscreen);
        if (status == Result.Ok)
            return new Specs(keyboard, mouse, touchscreen);
        ReportError(status, "profile spec creation");
        if (keyboard != nint.Zero)
            Native.SpecRelease(keyboard);
        if (mouse != nint.Zero)
            Native.SpecRelease(mouse);
        if (touchscreen != nint.Zero)
            Native.SpecRelease(touchscreen);
        return null;
    }

    private static void ReleaseSpecs(Specs specs)
    {
        Native.SpecRelease(specs.Keyboard);
        Native.SpecRelease(specs.Mouse);
        Native.SpecRelease(specs.Touchscreen);
    }

    private static void ReportError(Result status, string operation)
    {
        // ResultName is another public call and clears the calling thread's
        // diagnostic record, so copy both borrowed strings into managed storage first.
        var pointer = Native.LastError();
        var field = "no field";
        var reason = "no detail";
        if (pointer != nint.Zero)
        {
            var detail = Marshal.PtrToStructure<ErrorDetail>(pointer);
            field = Marshal.PtrToStringUTF8(detail.Field) ?? field;
            reason = Marshal.PtrToStringUTF8(detail.Reason) ?? reason;
        }
        var resultName = Marshal.PtrToStringUTF8(Native.ResultName(status)) ?? status.ToString();
        if (pointer == nint.Zero)
        {
            Console.Error.WriteLine($"{operation}: {resultName}");
            return;
        }
        Console.Error.WriteLine($"{operation}: {resultName} ({field}: {reason})");
    }

    private static bool TryDiscoveryInfo(
        nint discovery, nuint index, out DeviceInfo info, out Result status)
    {
        var pointer = Native.DiscoveryGet(discovery, index);
        if (pointer != nint.Zero)
        {
            info = Marshal.PtrToStructure<DeviceInfo>(pointer);
            status = Result.Ok;
            return true;
        }
        info = new DeviceInfo();
        var detailPointer = Native.LastError();
        status = detailPointer == nint.Zero
            ? Result.Internal
            : (Result)Marshal.PtrToStructure<ErrorDetail>(detailPointer).Code;
        if (status == Result.Ok)
            status = Result.Internal;
        return false;
    }

    private static bool TryDiscoveryCount(
        nint discovery, out nuint count, out Result status)
    {
        count = Native.DiscoveryCount(discovery);
        if (count != 0)
        {
            status = Result.Ok;
            return true;
        }
        var detailPointer = Native.LastError();
        if (detailPointer == nint.Zero)
        {
            status = Result.Internal;
            return false;
        }
        status = (Result)Marshal.PtrToStructure<ErrorDetail>(detailPointer).Code;
        return status == Result.Ok;
    }

    private static byte[] ReportedPortPath(DeviceInfo info)
    {
        var length = checked((int)info.PortPathLength);
        if (length == 0 || info.PortPath == nint.Zero)
            return Array.Empty<byte>();
        var ports = new byte[length];
        Marshal.Copy(info.PortPath, ports, 0, length);
        return ports;
    }

    private static Locator? LocatorFrom(DeviceInfo info)
    {
        var ports = ReportedPortPath(info);
        // This caller never falls back to ambiguous bus-only reconnection matching.
        return ports.Length == 0
            ? null
            : new Locator(info.BusNumber, ports, Marshal.PtrToStringUTF8(info.Serial) ?? "");
    }

    private static bool Matches(Locator locator, DeviceInfo info)
    {
        var candidate = LocatorFrom(info);
        return candidate is not null && candidate.BusNumber == locator.BusNumber &&
            candidate.PortPath.AsSpan().SequenceEqual(locator.PortPath) &&
            (locator.Serial.Length == 0 || candidate.Serial == locator.Serial);
    }

    private static void PrintInfo(DeviceInfo info)
    {
        var ports = string.Join(".", ReportedPortPath(info).Select(value => value.ToString()));
        Console.WriteLine(
            $"bus={info.BusNumber} address={info.DeviceAddress} port={ports} " +
            $"vid:pid={info.VendorId:x4}:{info.ProductId:x4} aoa={info.ProtocolVersion} " +
            $"product={Marshal.PtrToStringUTF8(info.Product) ?? ""} " +
            $"serial={Marshal.PtrToStringUTF8(info.Serial) ?? ""}");
    }

    private static Session? OpenSession(
        nint context, DeviceInfo info, Locator locator, Specs specs, uint timeoutMs,
        out Result openStatus)
    {
        // Open sends no AOA request, so the version check is the caller's.
        if (info.ProtocolVersion < 2)
        {
            Console.Error.WriteLine(
                $"device open: AOA {info.ProtocolVersion} has no HID; version 2 is required");
            openStatus = Result.NotAoa;
            return null;
        }
        var deviceOptions = DeviceConfiguration(timeoutMs);
        var status = Native.DeviceOpen(context, in info, in deviceOptions, out var device);
        if (status != Result.Ok)
        {
            ReportError(status, "device open");
            openStatus = status;
            return null;
        }
        var session = new Session(locator) { Device = device };
        var nodeOptions = NodeConfiguration();
        status = Native.NodeOpen(device, specs.Keyboard, in nodeOptions, out var keyboard);
        session.Keyboard = keyboard;
        if (status == Result.Ok)
        {
            status = Native.NodeOpen(device, specs.Mouse, in nodeOptions, out var mouse);
            session.Mouse = mouse;
        }
        if (status == Result.Ok)
        {
            status = Native.NodeOpen(device, specs.Touchscreen, in nodeOptions, out var touchscreen);
            session.Touchscreen = touchscreen;
        }
        if (status == Result.Ok)
        {
            openStatus = Result.Ok;
            return session;
        }
        ReportError(status, "profile registration");
        CloseDevice(session);
        openStatus = status;
        return null;
    }

    private static Result CloseDevice(Session session)
    {
        if (session.Device == nint.Zero)
            return Result.Ok;
        var status = Native.DeviceClose(session.Device);
        // The first close consumes Device and all Node handles for every result.
        session.Device = nint.Zero;
        session.Keyboard = nint.Zero;
        session.Mouse = nint.Zero;
        session.Touchscreen = nint.Zero;
        if (status is not (Result.Ok or Result.ClosePending or Result.NoDevice))
            ReportError(status, "device close");
        return status;
    }

    private static Result CloseTouchscreen(nint context, Session session, uint pollMs)
    {
        while (session.Touchscreen != nint.Zero)
        {
            var status = Native.NodeClose(session.Touchscreen);
            if (status == Result.Ok)
            {
                session.Touchscreen = nint.Zero;
                return status;
            }
            if (status != Result.ClosePending)
            {
                ReportError(status, "touchscreen close");
                return status;
            }
            // Pending Node close retains ownership, so polling and retry are required.
            var poll = Native.ContextPoll(context, pollMs);
            if (poll != Result.Ok)
                return poll;
        }
        return Result.Ok;
    }

    private static Result DestroyContext(nint context, uint pollMs)
    {
        while (context != nint.Zero)
        {
            var status = Native.ContextDestroy(context);
            if (status != Result.ClosePending)
                return status; // Every non-pending valid destroy result consumes Context.
            var poll = Native.ContextPoll(context, pollMs);
            if (poll != Result.Ok)
                ReportError(poll, "context poll during shutdown");
        }
        return Result.Ok;
    }

    private static Result FirstFailure(params Result[] statuses) =>
        statuses.FirstOrDefault(status => status != Result.Ok);

    private static (List<Locator> Disappeared, Result Status) UpdateThenSubmit(
        List<Session> sessions, uint timeoutMs)
    {
        var disappeared = new List<Locator>();
        var result = Result.Ok;
        var ready = new List<Session>();
        var contact = new TouchContact
        {
            ContactId = TouchContactId,
            X = ExampleTouchX,
            Y = ExampleTouchY,
            Pressure = 0,
            Width = 0,
            Height = 0,
            Azimuth = 0,
        };
        // Prepare all state first; USB submission is a separate pass so one
        // device does not delay the next device's preparation.
        foreach (var session in sessions.Where(value => value.Device != nint.Zero))
        {
            var status = FirstFailure(
                Native.KeyboardKey(session.Keyboard, KeyboardAUsage, 1),
                Native.MouseMove(session.Mouse, ExampleMouseDx, ExampleMouseDy),
                Native.TouchscreenContact(session.Touchscreen, contact.ContactId, 1, contact.X, contact.Y, default));
            if (status == Result.NoDevice)
            {
                disappeared.Add(session.Locator);
                CloseDevice(session);
            }
            else if (status == Result.Ok)
            {
                ready.Add(session);
            }
            else
            {
                ReportError(status, "profile update");
                if (result == Result.Ok)
                    result = status;
            }
        }
        foreach (var session in ready)
        {
            var status = FirstFailure(
                Native.NodeSubmitBlocking(session.Keyboard, timeoutMs),
                Native.NodeSubmitBlocking(session.Mouse, timeoutMs),
                Native.NodeSubmitBlocking(session.Touchscreen, timeoutMs));
            if (status == Result.NoDevice)
            {
                disappeared.Add(session.Locator);
                CloseDevice(session);
            }
            else if (status != Result.Ok)
            {
                ReportError(status, "profile submit");
                if (result == Result.Ok)
                    result = status;
            }
        }
        return (disappeared, result);
    }

    private static Result Reopen(
        nint context, List<Session> sessions, List<Locator> pending, Specs specs, uint timeoutMs)
    {
        if (pending.Count == 0)
            return Result.Ok;
        var status = Native.Discover(context, timeoutMs, out var discovery);
        if (status != Result.Ok)
            return status;
        try
        {
            var hasCount = TryDiscoveryCount(discovery, out var count, out var countStatus);
            if (!hasCount)
                status = countStatus;
            for (nuint index = 0; hasCount && index < count; ++index)
            {
                if (!TryDiscoveryInfo(discovery, index, out var info, out var entryStatus))
                {
                    status = entryStatus;
                    break;
                }
                var locator = pending.FirstOrDefault(value => Matches(value, info));
                if (locator is null)
                    continue;
                var reopened = OpenSession(
                    context, info, locator, specs, timeoutMs, out var openStatus);
                if (reopened is not null)
                {
                    sessions.Add(reopened);
                    pending.Remove(locator);
                }
                else if (status == Result.Ok)
                {
                    status = openStatus;
                }
            }
        }
        finally
        {
            Native.DiscoveryDestroy(discovery);
        }
        if (pending.Count != 0 && status == Result.Ok)
            status = Result.NoDevice;
        return status;
    }

    private static Result ContinueWithoutTouch(nint context, List<Session> sessions, uint timeoutMs)
    {
        var result = Result.Ok;
        foreach (var session in sessions.Where(value => value.Device != nint.Zero))
        {
            var close = CloseTouchscreen(context, session, timeoutMs);
            if (close != Result.Ok)
                result = close;
        }
        var ready = new List<Session>();
        foreach (var session in sessions.Where(value => value.Device != nint.Zero))
        {
            var status = FirstFailure(
                Native.KeyboardKey(session.Keyboard, KeyboardAUsage, 0),
                Native.MouseButton(session.Mouse, 1, 1)); // Caller selects pointer button 1.
            if (status == Result.Ok)
                ready.Add(session);
            else if (status == Result.NoDevice)
                CloseDevice(session);
            else
                result = status;
        }
        foreach (var session in ready)
        {
            var status = FirstFailure(
                Native.NodeSubmitBlocking(session.Keyboard, timeoutMs),
                Native.NodeSubmitBlocking(session.Mouse, timeoutMs));
            if (status != Result.Ok)
                result = status;
        }
        return result;
    }

    private static int RunShared(uint timeoutMs)
    {
        var contextOptions = ContextConfiguration();
        var status = Native.ContextCreate(in contextOptions, out var context);
        if (status != Result.Ok)
            return (int)status;
        Specs? specs = null;
        var sessions = new List<Session>();
        var result = Result.Ok;
        try
        {
            specs = CreateSpecs();
            if (specs is null)
                return (int)Result.Internal;
            status = Native.Discover(context, timeoutMs, out var discovery);
            if (status != Result.Ok)
                return (int)status;
            try
            {
                var hasCount = TryDiscoveryCount(discovery, out var count, out var countStatus);
                if (!hasCount && result == Result.Ok)
                    result = countStatus;
                for (nuint index = 0; hasCount && index < count; ++index)
                {
                    if (!TryDiscoveryInfo(discovery, index, out var info, out var entryStatus))
                    {
                        if (result == Result.Ok)
                            result = entryStatus;
                        break;
                    }
                    PrintInfo(info);
                    var locator = LocatorFrom(info);
                    if (locator is null)
                    {
                        Console.Error.WriteLine(
                            "stable device locator: caller policy requires a physical port path");
                        if (result == Result.Ok)
                            result = Result.Unsupported;
                        continue;
                    }
                    var session = OpenSession(
                        context, info, locator, specs, timeoutMs, out var openStatus);
                    if (session is not null)
                        sessions.Add(session);
                    else if (result == Result.Ok)
                        result = openStatus;
                }
            }
            finally
            {
                Native.DiscoveryDestroy(discovery);
            }
            var drive = UpdateThenSubmit(sessions, timeoutMs);
            if (result == Result.Ok)
                result = drive.Status;
            // Only missing sessions are reopened; live siblings remain open.
            status = Reopen(context, sessions, drive.Disappeared, specs, timeoutMs);
            if (result == Result.Ok)
                result = status;
            var continuation = ContinueWithoutTouch(context, sessions, timeoutMs);
            if (result == Result.Ok)
                result = continuation;
        }
        finally
        {
            foreach (var session in sessions)
                CloseDevice(session);
            if (specs is not null)
                ReleaseSpecs(specs);
            var close = DestroyContext(context, timeoutMs);
            if (result == Result.Ok)
                result = close;
        }
        return (int)result;
    }

    private static (Result Status, List<Locator> Locators) EnumerateLocators(uint timeoutMs)
    {
        var options = ContextConfiguration();
        var status = Native.ContextCreate(in options, out var context);
        var locators = new List<Locator>();
        if (status != Result.Ok)
            return (status, locators);
        try
        {
            status = Native.Discover(context, timeoutMs, out var discovery);
            if (status != Result.Ok)
                return (status, locators);
            try
            {
                var hasCount = TryDiscoveryCount(discovery, out var count, out var countStatus);
                if (!hasCount)
                    status = countStatus;
                for (nuint index = 0; hasCount && index < count; ++index)
                {
                    if (!TryDiscoveryInfo(discovery, index, out var info, out var entryStatus))
                    {
                        status = entryStatus;
                        break;
                    }
                    PrintInfo(info);
                    var locator = LocatorFrom(info);
                    if (locator is null)
                    {
                        Console.Error.WriteLine(
                            "stable device locator: caller policy requires a physical port path");
                        status = Result.Unsupported;
                        break;
                    }
                    locators.Add(locator);
                }
            }
            finally
            {
                Native.DiscoveryDestroy(discovery);
            }
        }
        finally
        {
            var close = DestroyContext(context, timeoutMs);
            if (status == Result.Ok)
                status = close;
        }
        return (status, locators);
    }

    private static int RunWorker(Locator locator, uint timeoutMs)
    {
        var options = ContextConfiguration();
        var status = Native.ContextCreate(in options, out var context);
        if (status != Result.Ok)
            return (int)status;
        Specs? specs = null;
        var sessions = new List<Session>();
        var result = Result.Ok;
        try
        {
            specs = CreateSpecs();
            if (specs is null)
            {
                result = Result.Internal;
            }
            else
            {
                status = Native.Discover(context, timeoutMs, out var discovery);
                if (status != Result.Ok)
                {
                    result = status;
                }
                else
                {
                    try
                    {
                        var hasCount = TryDiscoveryCount(
                            discovery, out var count, out var countStatus);
                        if (!hasCount)
                            result = countStatus;
                        for (nuint index = 0; hasCount && index < count; ++index)
                        {
                            if (!TryDiscoveryInfo(
                                    discovery, index, out var info, out var entryStatus))
                            {
                                result = entryStatus;
                                break;
                            }
                            if (!Matches(locator, info))
                                continue;
                            var session = OpenSession(
                                context, info, locator, specs, timeoutMs, out var openStatus);
                            if (session is not null)
                                sessions.Add(session);
                            else
                                result = openStatus;
                            break;
                        }
                    }
                    finally
                    {
                        Native.DiscoveryDestroy(discovery);
                    }
                    if (sessions.Count == 0 && result == Result.Ok)
                        result = Result.NoDevice;
                    var drive = UpdateThenSubmit(sessions, timeoutMs);
                    if (result == Result.Ok)
                        result = drive.Status;
                    // Reconnection stays inside this thread/context; sibling workers continue.
                    status = Reopen(context, sessions, drive.Disappeared, specs, timeoutMs);
                    if (result == Result.Ok)
                        result = status;
                    var continuation = ContinueWithoutTouch(context, sessions, timeoutMs);
                    if (result == Result.Ok)
                        result = continuation;
                }
            }
        }
        finally
        {
            foreach (var session in sessions)
                CloseDevice(session);
            if (specs is not null)
                ReleaseSpecs(specs);
            var close = DestroyContext(context, timeoutMs);
            if (result == Result.Ok)
                result = close;
        }
        return (int)result;
    }

    private static int RunThreaded(uint timeoutMs)
    {
        var enumeration = EnumerateLocators(timeoutMs);
        if (enumeration.Status != Result.Ok)
            return (int)enumeration.Status;
        var workers = enumeration.Locators.Select(locator => new Worker(locator, timeoutMs)).ToList();
        // The worker count is exactly the runtime discovery count; no ceiling is encoded.
        var threads = workers.Select(worker => new Thread(worker.Run)).ToList();
        foreach (var thread in threads)
            thread.Start();
        foreach (var thread in threads)
            thread.Join();
        return workers.Select(worker => worker.ExitCode).FirstOrDefault(value => value != 0);
    }
}
