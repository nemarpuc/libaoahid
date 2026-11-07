# Changelog

All notable changes to libaoahid are recorded here. This project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-11-07

Not hardware-verified on a phone in this form: the transport and the device
profiles were verified on a Samsung Galaxy Tab S11 (Arch Linux) via devtest and
kernel getevent (Keyboard, Mouse, Toggle, Gamepad, Touchscreen, Touchpad, Pen,
Digitizer); the API around them has changed since. The current library is
tested against the fake libusb backend (including a ThreadSanitizer build) and
the bindings against the compiled C ABI.

- A C++20 library that sends Android Open Accessory 2.0 HID reports to a phone
  over libusb, behind a stable C ABI (`aoahid.h`, 53 exported functions,
  SONAME `libaoahid.so.0`, ELF version node `AOAHID_0.0`).
- Object model: a Context (caller-polled or with an internal event thread) owns
  Devices; a Device carries Nodes (one AOA HID ID per immutable Spec, built from
  generated input profiles or a raw descriptor) and Channels (bulk interfaces
  such as ADB).
- No inferred values: Usages, ranges, widths, Report IDs and every timeout and
  buffer size are set by the caller; an unset value is an error.
- `aoahid.hpp`: a header-only C++ facade forwarding to every C function.
- Bindings generated from the C header for Python (ctypes), C# (P/Invoke) and
  Rust (`aoahid-sys` and the typed `aoahid` crate).
- `find_package(aoahid 0.1)` is met by a 0.1.x install only: before 1.0.0 a change of the minor
  number may change the API or the ABI.
