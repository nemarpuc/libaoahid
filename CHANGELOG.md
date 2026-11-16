# Changelog

All notable changes to libaoahid are recorded here. This project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- Bindings: the Python, C# and Rust bindings carry the values of the C `aoahid_*_init` functions
  (`aoahid.initial`, the C# `Init()` methods, Rust `Default`), where they set only `struct_size`
  before; a test per language compares them byte for byte with the compiled C initializers
  (`tests/abi/init_oracle.c`). The C# `Init()` of the twelve structures whose C initializer fills
  values now fills them too.

- Docs: `docs/PROTOCOL.md` says the kernel passes a request 57 payload to `hid_report_raw_event`
  (it named `hid_input_report`) and that `f_accessory.c` does not use `uhid`; `docs/VERIFICATION.md`
  and `docs/LATENCY.md` no longer name `uhid`, and `docs/LATENCY.md` names
  `acc_complete_send_hid_event`.

### Added

- Bindings: owning objects, so a program needs no hand-written release code: Python `aoahid.Session`,
  C# `AoaSession`, Rust `Context`, `Device` and `Node`. They open a Node's Spec and release it in the
  right order, wait 100 ms after opening Nodes (`docs/API.md` "Open sequence"), refuse a native library
  of another version, and refuse `open_only` unless exactly one USB device is present. Short keyboard
  examples in the three languages; the former ones are kept as `keyboard_raw.py`, `examples/csharp/KeyboardRaw`
  and `aoahid-sys/examples/keyboard.rs`.
- Docs: `docs/PROTOCOL.md` section "Report IDs and one HID device per Node": Report IDs in a
  descriptor, one `hid_device` per Node, why several Specs are not joined into one HID device, and
  how Android classifies and identifies the resulting `evdev` devices, each with its source commit.

## [0.2.1] - 2026-11-15

**Not hardware-verified.** Nothing in this release changes how the library behaves: header comments, binding helpers and examples only. No phone was used; the keyboard examples were only built by CI. The only hardware record is the `[0.1.0]` entry below.

### Changed

- `include/aoahid.h`: a function comment now starts with a one-sentence summary and lists
  Ownership, Blocking and Synchronization only where they differ from the defaults stated at the
  top of the header; Returns is unchanged.
- `tools/check_public_contracts.py` requires the summary line and one `Returns:` field; the other
  three fields are optional.
- Bindings: `tools/bindings/generate.py` carries the first sentence of each function and structure
  comment into the generated Python docstrings, C# XML summaries and Rust doc comments.

### Added

- Examples: a minimal `keyboard` program for Python, C# and Rust that presses the letter `a` on
  the first phone found.
- Python: a structure that has `struct_size` is created with it set to its size (every other field
  stays zero); `aoahid.check(library, result)` raises `aoahid.Error` with the result name and the
  `field` and `reason` of the last error when a call does not return `AOAHID_OK`.
- C#: `Init()` on every structure that has `StructSize` returns a zeroed structure with it set;
  `AoaHidException.Check(result)` throws with the result and the details of the last error.

## [0.2.0] - 2026-11-10

**Not hardware-verified.** This release changes the release packaging, the tests, the documentation and one
argument check; none of it was run on a phone. The only hardware record is the `[0.1.0]` entry below. Asset
names changed: a script that downloads the old `-shared` / `-static` names must be changed.


- Fix: `aoahid_spec_create_mouse`, `aoahid_spec_create_touchscreen`, and
  `aoahid_spec_create_touchpad` now return the result code recorded in
  `aoahid_last_error()` when an enabled wheel, AC Pan, Contact Width, or
  Contact Height field is invalid—in particular returning
  `AOAHID_ERR_UNSET_FIELD` (instead of `AOAHID_ERR_PARAM`) when `bit_width` is
  zero (`src/profiles/spec.cpp`).
- Docs: added `docs/BUILDING.md`, `docs/RELEASES.md`, and
  `docs/VERIFICATION.md`, expanded the design rationales in
  `docs/ARCHITECTURE.md` and `docs/INTERNALS.md`, added measurement methods to
  `docs/LATENCY.md`, and recorded three platform-specific open test items in
  `tests/README.md`, `docs/LIMITS.md`, and `docs/VERIFICATION.md`.
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
