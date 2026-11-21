# AoaHid .NET binding

This NuGet package contains only managed code: the literal P/Invoke declarations of the
libaoahid C ABI (`Native`), the values of the C `aoahid_*_init` functions (the `Init()` methods) and
owning objects (`AoaSession`). It does not contain `aoahid.dll`, `libaoahid.so`, libusb, or
other native runtime assets. Install the matching libaoahid shared distribution
separately and make it available to the .NET native-library loader under the
name `aoahid`.

```csharp
using var session = new AoaSession();
var keyboard = session.OpenOnly().Keyboard();   // the one USB device; any other count throws
keyboard.Tap(0x04);                             // key down, send, key up, send
```

- **Raw layer.** `Native` chooses nothing. The generated `Init()` of a structure the C initializer
  fills only with `struct_size` (for example `NodePlacement`) returns a zeroed structure with
  `StructSize` set.
- **Initial values.** `Init()` of `ContextOptions`, `DeviceOptions`, `AccessoryOptions`,
  `ChannelOptions` and the profile options returns the values of the C `aoahid_*_init` function of
  the same name (`docs/API.md` lists them); a test compares them byte for byte with the compiled C
  initializers. Set a field to change it.
- **Owning objects.** `AoaSession`, `AoaDevice` and the node classes release what they opened in
  reverse order on `Dispose`. `OpenOnly()` throws unless exactly one USB device is present; with
  several, use `Discover()` and `Open(index)`. Opening a Node waits `settle` (default 100 ms,
  `docs/API.md` "Open sequence") before the first report. Calls are not thread-safe: the
  application serializes calls on a Context. `Handle` gives the raw handle for `Native` calls.

The NuGet package version and native libaoahid version must match; `AoaSession` checks it. Complete
native archives, dependency notices, and SPDX documents are published with the corresponding
GitHub Release. A required field left zero is refused with `Result.UnsetField`.

## Callback lifetime

`ContextOptions.LogSink` holds a native function pointer, typically obtained
with `Marshal.GetFunctionPointerForDelegate(sink)`. The garbage collector does
not know that native code still uses it: keep the `LogSink` delegate instance
referenced, for example in a field, until `ContextDestroy` has returned, or a
later log call may run freed code and crash the process.

## Failed calls

`AoaHidException.Check(result)` does nothing for `Result.Ok` and otherwise throws
with the result and the `Field` and `Reason` of the library's last error. Call
it right after the call that returned `result`.
