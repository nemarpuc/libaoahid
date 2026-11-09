# Changelog

All notable changes to libaoahid are recorded here. This project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

- Tests: the fake-libusb tests are called what they are, `sim` (`tests/sim`,
  ctest `aoahid.unit`, `aoahid.sim`, `aoahid.sim-*`); `hw` is kept for runs on
  a real phone.
- Build: `AOAHID_COVERAGE` (GCC or Clang, off by default, not combinable with
  the sanitizers or fuzzing), a `coverage` preset and a CI job "Coverage" that
  writes the gcovr table for `src/` into the job summary and keeps the report.
- Tests: new suites for every option field of each profile's Spec, every
  public argument check, the input calls' state rules, transport and Channel
  failures and raw-descriptor faults, and a sim run that fails each
  allocation in turn. The Coverage job fails below 87% of lines or 75% of
  branches.
- **The release no longer publishes `*-runtime` bundles.** The `*-shared`
  archive already carries the shared library, the libusb runtime, and the same
  license material (`LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.md`, libusb's
  LGPL-2.1 text and corresponding source), and nothing consumed the smaller
  bundle. The architecture and glibc checks that ran on the bundle now run on
  the `*-shared` archive itself, and `release-manifest.json` drops the
  `runtime_bundle`, `runtime_bundle_sha256`, and
  `runtime_glibc_maximum_required` keys. A release now has 27 assets instead
  of 33; the `*-shared` and `*-static` asset names are unchanged.

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
