// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
// Owning objects over Native: a Context, a Device and its Nodes, released in reverse order.
// Nothing is picked for the caller: OpenOnly refuses unless exactly one USB device is present.
// Calls are not thread-safe, as the C ABI requires (the application serializes calls on a Context).
using System.Runtime.InteropServices;

namespace AoaHid;

/// <summary>One USB device a discovery found.</summary>
public sealed record DeviceSummary(ushort VendorId, ushort ProductId, string? Serial, string? Product);

/// <summary>A Context with the options of ContextOptions.Init() (an internal event thread, no logging) unless others are given.</summary>
public sealed class AoaSession : IDisposable
{
    /// <summary>docs/API.md "Open sequence" step 5: Android gives no sign when it has registered the HID, so leave a short gap; the README waits 100 ms.</summary>
    public static readonly TimeSpan Settle = TimeSpan.FromMilliseconds(100);

    /// <summary>The waits of a Node open or close: the library's control and drain times.</summary>
    internal static CommitTimeouts Timeouts()
    {
        var timeouts = CommitTimeouts.Init();
        timeouts.ControlTimeoutMs = Native.AOAHID_CONTROL_TIMEOUT_MS;
        timeouts.DrainTimeoutMs = Native.AOAHID_DRAIN_TIMEOUT_MS;
        return timeouts;
    }

    private readonly List<AoaDevice> _devices = new();
    private nint _context;
    private nint _discovery;
    private ContextOptions _options;

    public AoaSession(ContextOptions? options = null)
    {
        var expected = (Native.AOAHID_VERSION_MAJOR << 24) | (Native.AOAHID_VERSION_MINOR << 16) | Native.AOAHID_VERSION_PATCH;
        var found = Native.Version();
        if (found != expected)
            throw new AoaHidException(Result.Unsupported, "version",
                $"the library is version {found >> 24}.{(found >> 16) & 0xFF}.{found & 0xFFFF}, this package is "
                + $"{Native.AOAHID_VERSION_MAJOR}.{Native.AOAHID_VERSION_MINOR}.{Native.AOAHID_VERSION_PATCH}");
        _options = options ?? ContextOptions.Init();
        AoaHidException.Check(Native.ContextCreate(in _options, out _context));
    }

    public nint Handle => _context;

    /// <summary>The USB devices found now; <see cref="Open"/> opens one of them by index.</summary>
    public IReadOnlyList<DeviceSummary> Discover()
    {
        DropDiscovery();
        AoaHidException.Check(Native.Discover(_context, Native.AOAHID_CONTROL_TIMEOUT_MS, out _discovery));
        var found = new List<DeviceSummary>();
        for (nuint i = 0; i < Native.DiscoveryCount(_discovery); i++)
        {
            var info = Marshal.PtrToStructure<DeviceInfo>(Native.DiscoveryGet(_discovery, i));
            found.Add(new DeviceSummary(info.VendorId, info.ProductId, Marshal.PtrToStringUTF8(info.Serial),
                                        Marshal.PtrToStringUTF8(info.Product)));
        }
        return found;
    }

    /// <summary>Opens device <paramref name="index"/> of the last <see cref="Discover"/>.</summary>
    public AoaDevice Open(int index, DeviceOptions? options = null)
    {
        if (_discovery == 0 || index < 0 || (nuint)index >= Native.DiscoveryCount(_discovery))
            throw new ArgumentOutOfRangeException(nameof(index), "no such device in the last Discover()");
        var info = Marshal.PtrToStructure<DeviceInfo>(Native.DiscoveryGet(_discovery, (nuint)index));
        var deviceOptions = options ?? DeviceOptions.Init();
        AoaHidException.Check(Native.DeviceOpen(_context, in info, in deviceOptions, out var raw));
        var device = new AoaDevice(this, raw);
        _devices.Add(device);
        return device;
    }

    /// <summary>Discovers and opens the one device present; any other count throws.</summary>
    public AoaDevice OpenOnly(DeviceOptions? options = null)
    {
        var found = Discover();
        if (found.Count != 1)
            throw new AoaHidException(Result.Unsupported, "devices",
                $"expected exactly one USB device, found {found.Count}; use Discover() and Open(index)");
        return Open(0, options);
    }

    private void DropDiscovery()
    {
        if (_discovery != 0)
        {
            Native.DiscoveryDestroy(_discovery);
            _discovery = 0;
        }
    }

    public void Dispose()
    {
        for (var i = _devices.Count - 1; i >= 0; i--)
            _devices[i].Dispose();
        _devices.Clear();
        DropDiscovery();
        if (_context != 0)
        {
            var t = Native.AOAHID_CONTROL_TIMEOUT_MS;
            Native.ContextDestroyBlocking(_context, t, t, t);
            _context = 0;
        }
    }
}

/// <summary>An open device; disposing it closes its Nodes first.</summary>
public sealed class AoaDevice : IDisposable
{
    private readonly List<AoaNode> _nodes = new();
    private nint _device;
    private uint _registrations; // the last registration number given out

    internal AoaDevice(AoaSession session, nint device)
    {
        Session = session;
        _device = device;
    }

    public AoaSession Session { get; }
    public nint Handle => _device;

    private T Open<T>(Func<(Result Result, nint Spec)> create, Func<nint, T> make, TimeSpan? settle) where T : AoaNode
    {
        var (result, spec) = create();
        AoaHidException.Check(result);
        try
        {
            // Each Node is a HID device of its own on the phone.
            var placement = NodePlacement.Init();
            placement.Registration = ++_registrations;
            var timeouts = AoaSession.Timeouts();
            AoaHidException.Check(Native.NodeOpen(_device, spec, in placement, in timeouts, out var raw));
            var node = make(raw);
            _nodes.Add(node);
            Thread.Sleep(settle ?? AoaSession.Settle);
            return node;
        }
        finally
        {
            Native.SpecRelease(spec); // the Node keeps its own reference
        }
    }

    public KeyboardNode Keyboard(KeyboardOptions? options = null, TimeSpan? settle = null)
    {
        var o = options ?? KeyboardOptions.Init();
        return Open(() => (Native.SpecCreateKeyboard(in o, out var s), s), n => new KeyboardNode(n), settle);
    }

    public MouseNode Mouse(MouseOptions? options = null, TimeSpan? settle = null)
    {
        var o = options ?? MouseOptions.Init();
        return Open(() => (Native.SpecCreateMouse(in o, out var s), s), n => new MouseNode(n), settle);
    }

    public ToggleNode Toggle(ToggleOptions options, TimeSpan? settle = null)
    {
        return Open(() => (Native.SpecCreateToggle(in options, out var s), s), n => new ToggleNode(n), settle);
    }

    public GamepadNode Gamepad(GamepadOptions? options = null, TimeSpan? settle = null)
    {
        var o = options ?? GamepadOptions.Init();
        return Open(() => (Native.SpecCreateGamepad(in o, out var s), s), n => new GamepadNode(n), settle);
    }

    public TouchscreenNode Touchscreen(TouchscreenOptions? options = null, TimeSpan? settle = null)
    {
        var o = options ?? TouchscreenOptions.Init();
        return Open(() => (Native.SpecCreateTouchscreen(in o, out var s), s), n => new TouchscreenNode(n), settle);
    }

    public TouchpadNode Touchpad(TouchpadOptions? options = null, TimeSpan? settle = null)
    {
        var o = options ?? TouchpadOptions.Init();
        return Open(() => (Native.SpecCreateTouchpad(in o, out var s), s), n => new TouchpadNode(n), settle);
    }

    public PenNode Pen(PenOptions? options = null, TimeSpan? settle = null)
    {
        var o = options ?? PenOptions.Init();
        return Open(() => (Native.SpecCreatePen(in o, out var s), s), n => new PenNode(n), settle);
    }

    public DigitizerNode Digitizer(DigitizerOptions? options = null, TimeSpan? settle = null)
    {
        var o = options ?? DigitizerOptions.Init();
        return Open(() => (Native.SpecCreateDigitizer(in o, out var s), s), n => new DigitizerNode(n), settle);
    }

    /// <summary>A raw Node: the descriptor and report table in <paramref name="options"/> are the caller's.</summary>
    public RawNode Raw(RawOptions options, TimeSpan? settle = null)
    {
        return Open(() => (Native.SpecCreateRaw(in options, out var s), s), n => new RawNode(n), settle);
    }

    public void Dispose()
    {
        for (var i = _nodes.Count - 1; i >= 0; i--)
            _nodes[i].Dispose();
        _nodes.Clear();
        if (_device != 0)
        {
            Native.DeviceClose(_device, Native.AOAHID_CONTROL_TIMEOUT_MS, Native.AOAHID_DRAIN_TIMEOUT_MS);
            _device = 0;
        }
    }
}

/// <summary>An open Node; the profile classes add its state calls.</summary>
public abstract class AoaNode : IDisposable
{
    private nint _node;

    protected AoaNode(nint node) => _node = node;

    public nint Handle => _node;

    /// <summary>Queues the pending state without waiting for it to be sent.</summary>
    public void Submit(uint timeoutMs = Native.AOAHID_SEND_TIMEOUT_MS) => AoaHidException.Check(Native.NodeSubmit(_node, timeoutMs));

    public void SubmitBlocking(uint timeoutMs = Native.AOAHID_SEND_TIMEOUT_MS) =>
        AoaHidException.Check(Native.NodeSubmitBlocking(_node, timeoutMs));

    public void Battery(int strength) => AoaHidException.Check(Native.BatteryUpdate(_node, strength));

    public void Dispose()
    {
        if (_node != 0)
        {
            var timeouts = AoaSession.Timeouts();
            Native.NodeClose(_node, in timeouts);
            _node = 0;
        }
    }
}

public sealed class KeyboardNode : AoaNode
{
    internal KeyboardNode(nint node) : base(node) { }

    public void Key(ushort usage, bool down) => AoaHidException.Check(Native.KeyboardKey(Handle, usage, down ? 1u : 0u));

    /// <summary>Press, send, wait, release, send.</summary>
    public void Tap(ushort usage, TimeSpan? hold = null)
    {
        Key(usage, true);
        SubmitBlocking();
        Thread.Sleep(hold ?? TimeSpan.FromMilliseconds(50));
        Key(usage, false);
        SubmitBlocking();
    }
}

public sealed class MouseNode : AoaNode
{
    internal MouseNode(nint node) : base(node) { }

    public void Move(int dx, int dy) => AoaHidException.Check(Native.MouseMove(Handle, dx, dy));
    public void Scroll(int wheel, int pan = 0) => AoaHidException.Check(Native.MouseScroll(Handle, wheel, pan));
    public void Button(uint button, bool down) => AoaHidException.Check(Native.MouseButton(Handle, button, down ? 1u : 0u));
}

public sealed class ToggleNode : AoaNode
{
    internal ToggleNode(nint node) : base(node) { }

    public void Usage(ushort usage, bool down) => AoaHidException.Check(Native.ToggleUsage(Handle, usage, down ? 1u : 0u));
}

public sealed class GamepadNode : AoaNode
{
    internal GamepadNode(nint node) : base(node) { }

    public void Button(uint button, bool down) => AoaHidException.Check(Native.GamepadButton(Handle, button, down ? 1u : 0u));
    public void Axis(nuint axisIndex, int value) => AoaHidException.Check(Native.GamepadAxis(Handle, axisIndex, value));

    public void Dpad(bool up, bool down, bool right, bool left) =>
        AoaHidException.Check(Native.GamepadDpad(Handle, up ? 1u : 0u, down ? 1u : 0u, right ? 1u : 0u, left ? 1u : 0u));
}

public sealed class TouchscreenNode : AoaNode
{
    internal TouchscreenNode(nint node) : base(node) { }

    public void Contact(uint contactId, bool down, int x, int y) =>
        AoaHidException.Check(Native.TouchscreenContact(Handle, contactId, down ? 1u : 0u, x, y, 0));

    public void Contact(uint contactId, bool down, int x, int y, in TouchscreenExtra extra) =>
        AoaHidException.Check(Native.TouchscreenContact(Handle, contactId, down ? 1u : 0u, x, y, in extra));
}

public sealed class TouchpadNode : AoaNode
{
    internal TouchpadNode(nint node) : base(node) { }

    public void Contact(uint contactId, bool down, int x, int y) =>
        AoaHidException.Check(Native.TouchpadContact(Handle, contactId, down ? 1u : 0u, x, y, 0));

    public void Contact(uint contactId, bool down, int x, int y, in TouchpadExtra extra) =>
        AoaHidException.Check(Native.TouchpadContact(Handle, contactId, down ? 1u : 0u, x, y, in extra));

    public void Button(uint button, bool down) => AoaHidException.Check(Native.TouchpadButton(Handle, button, down ? 1u : 0u));
}

public sealed class PenNode : AoaNode
{
    internal PenNode(nint node) : base(node) { }

    public void Update(in PenSample sample) => AoaHidException.Check(Native.PenUpdate(Handle, in sample));
}

public sealed class DigitizerNode : AoaNode
{
    internal DigitizerNode(nint node) : base(node) { }

    public void Update(in DigitizerSample sample) => AoaHidException.Check(Native.DigitizerUpdate(Handle, in sample));
}

public sealed class RawNode : AoaNode
{
    internal RawNode(nint node) : base(node) { }

    public void Report(ReadOnlySpan<byte> data, uint timeoutMs = Native.AOAHID_SEND_TIMEOUT_MS)
    {
        var block = Marshal.AllocHGlobal(Math.Max(data.Length, 1));
        try
        {
            Marshal.Copy(data.ToArray(), 0, block, data.Length);
            AoaHidException.Check(Native.RawSubmit(Handle, block, (nuint)data.Length, timeoutMs));
        }
        finally
        {
            Marshal.FreeHGlobal(block);
        }
    }
}
