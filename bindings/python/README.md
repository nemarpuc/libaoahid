# aoahid Python binding

This wheel contains only Python code: the literal `ctypes` declarations of the libaoahid C ABI
(`aoahid.native`), the values of the C `aoahid_*_init` functions (`aoahid.initial`) and owning objects
(`aoahid.Session`). It deliberately contains no `.so`, `.dll`, `.dylib`, or libusb runtime. Install the
matching libaoahid shared distribution separately, then pass its exact path to `aoahid.load(path)` or
`aoahid.Session(path)`. The binding never guesses a native-library path.

```python
import aoahid

with aoahid.Session("/path/to/libaoahid.so") as session:
    keyboard = session.open_only().keyboard()   # the one USB device; any other count is an error
    keyboard.tap(0x04)                          # key down, send, key up, send
```

- **Raw layer.** A `ctypes` structure created without positional arguments has only `struct_size` set;
  every other field is zero. `aoahid.native` chooses nothing.
- **Initial values.** `aoahid.initial.device_options()` and its siblings return the structure with the
  values of the C `aoahid_*_init` function of the same name (`docs/API.md` lists them); a keyword
  argument replaces one field. A test compares them byte for byte with the compiled C initializers.
- **Owning objects.** `Session`, its `Device` and their `Node`s release what they opened in reverse
  order. `open_only()` refuses unless exactly one USB device is present; with several, use
  `discover()` and `open(index)`. Every object has `.raw`, the `ctypes` handle, to drop to the raw calls.
  Opening a Node waits `settle` seconds (default 0.1, `docs/API.md` "Open sequence") before the first
  report. Calls are not thread-safe: the application serializes calls on a Context.

The Python package version and the native libaoahid version must match; `Session` checks it at load.
Complete native archives, dependency notices, and SPDX documents are published with the corresponding
GitHub Release.

`aoahid.check(library, result)` returns `result` when it is `AOAHID_OK` and
otherwise raises `aoahid.Error` carrying the result's name and the `field` and
`reason` of the library's last error. Call it right after the call; a result you
expect (for example `AOAHID_CLOSE_PENDING`) is compared by hand instead.
