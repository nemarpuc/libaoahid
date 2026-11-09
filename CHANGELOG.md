# Changelog

All notable changes to libaoahid are recorded here. This project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

- **The release no longer publishes `*-runtime` bundles.** The `*-shared`
  archive already carries the shared library, the libusb runtime, and the same
  license material (`LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.md`, libusb's
  LGPL-2.1 text and corresponding source), and nothing consumed the smaller
  bundle. The architecture and glibc checks that ran on the bundle now run on
  the `*-shared` archive itself, and `release-manifest.json` drops the
  `runtime_bundle`, `runtime_bundle_sha256`, and
  `runtime_glibc_maximum_required` keys. A release now has 27 assets instead
  of 33; the `*-shared` and `*-static` asset names are unchanged.
- **One archive per platform, one SBOM per release: 10 assets instead of 27.**
  Each target is built once with both `AOAHID_BUILD_SHARED` and
  `AOAHID_BUILD_STATIC` and ships as `libaoahid-X.Y.Z-<platform>-<arch>[-ubuntu22.04]`
  (`.tar.gz`, `.zip` on Windows) with no `-shared`/`-static` suffix. The archive
  holds both libraries, the CMake package with both `shared` and `static`
  components, `aoahid.pc` and `aoahid-static.pc`, the libusb runtime, header,
  and link library, the examples, and the same LGPL material as before. The
  static library is now `libaoahid_static.a` on Linux and macOS (it was
  `libaoahid.a` in the static-only archive). The twelve per-archive SPDX
  sidecars are replaced by `libaoahid-X.Y.Z.spdx.json`, one SPDX 2.3 document
  with a checksummed package per archive, generated from the source archive;
  each archive still embeds its own SPDX document. In `release-manifest.json`
  each target drops `variant`, `spdx`, and `spdx_sha256`, and gains
  `components`, `embedded_spdx_sha256`, and `embedded_spdx_namespace`; a
  top-level `sbom` record is added. `release.yml` also accepts a
  `workflow_dispatch` dry run that builds and checks everything without a tag,
  keeps the assembled release as the `release-dist` artifact, and publishes
  nothing.

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
