# libaoahid

[Canonical repository](https://github.com/nemarpuc/libaoahid)

Send keyboard, mouse, gamepad, multi-touch, and media-key input from a PC to
an Android device over a plain USB cable. The device needs no root, no app,
and no USB debugging: it sees an ordinary USB keyboard or touchscreen.
Windows and Linux, and macOS builds that are not yet tested with a device;
usable from C, C++, Python, C#, and Rust.

`libaoahid` is a C++20 library with a stable C ABI. It drives the HID feature
of [Android Open Accessory 2.0](https://source.android.com/docs/core/interaction/accessories/aoa2)
over libusb. You describe each input device (ranges, bit widths, Report IDs,
Usages) explicitly; the library builds a valid HID descriptor, registers it
with Android, and keeps the report state machines (multi-touch contacts,
key rollover, pointer deltas) correct. It never guesses a value you did not
set. MIT licensed.

## Demo

https://github.com/user-attachments/assets/880c5a81-bc1e-406b-9286-1a0f3dbf9899

The app in the video is
[aoahid_player](https://github.com/nemarpuc/aoahid_player), a script player
and recorder built on this library.

For an overview of how the library works and why it was built, see
[the introduction on DEV](https://dev.to/nemarpuc/libaoahid-a-c-abi-library-for-sending-hid-input-to-android-over-usb-1h6f).

## Hardware status

Tested on a Samsung Galaxy Tab S11 and a POCO F6 Pro, from Windows 10 x64 and
Arch Linux:

| Input | Tested on hardware |
|---|---|
| Touchscreen (multi-touch) | yes |
| Keyboard (full NKRO) | yes |
| Mouse (move, buttons, wheel) | yes |
| Gamepad (sticks, buttons, D-pad) | yes |
| Media keys (volume, play/pause, next/previous) | yes |
| Pen, touchpad, and other profiles | not yet |

Details per device and OS are in [TARGET_MATRIX.md](docs/TARGET_MATRIX.md).

## Windows drivers

On Windows, libusb can use a phone only through WinUSB (or libusbK / libusb0).
Input alone (HID, sent as USB control transfers) worked on the Samsung Galaxy
Tab S11 with Samsung's own driver. The ADB interface, a bulk interface, did
not, until the whole device was switched to WinUSB; the POCO F6 Pro had WinUSB
from the start. So if a phone cannot be opened, or you open a bulk interface
such as ADB, replacing the whole device's driver with WinUSB in
[Zadig](https://zadig.akeo.ie/) may fix it. That is not guaranteed on every
phone. Some manufacturers install their own driver; Samsung is one example.
See [PORTING.md](docs/PORTING.md#windows).

## Profiles

<!-- profile-table:start -->
| Profile | Manifest status | Input | Output | Feature transport | Qualification |
|---|---|---:|---:|---:|---|
| Keyboard, full NKRO bitmap | conditional | yes | no | no | One-bit Variable field per key; target matrix still required |
| Mouse / relative pointer | portable candidate | yes | no | no | Target matrix still required |
| Toggle: Consumer Control fields | conditional | yes | no | no | Sparse allow-list, explicit HUT semantics, and target event evidence |
| Toggle: System Control fields | conditional | yes | no | no | Explicit HUT semantics; system handling and target mapping vary |
| Gamepad | portable candidate | yes | no | no | Caller-declared axes and target mappings |
| Touchscreen, fixed MT | portable candidate | yes | no | no | Audited Linux commit and dated Android documentation; target matrix still required |
| Touchpad | conditional | yes | no | no | Distinct Linux input property (INPUT_PROP_POINTER); Android converts contacts to mouse-source motion, gesture value-add is OEM/release dependent |
| Pen, direct screen | portable candidate | yes | no | no | Invert tool transition; target matrix still required |
| Pen, indirect tablet | conditional | yes | no | no | Target classification and mapping evidence required |
| Toggle: Camera keys | conditional | yes | no | no | HUT Camera Auto-focus/Shutter subset only |
| Toggle: Telephony keys | conditional | yes | no | no | Caller allow-list and target mapping required |
| Battery Strength telemetry | conditional | yes | no | no | Kernel power-supply association and OEM dependent |
| Validated raw descriptor | unknown | yes | no | no | Explicit no-Android-support acknowledgement |
<!-- profile-table:end -->

"Manifest status" is the library's own classification of how portable a
descriptor shape is across Android targets; it is reported at runtime by the
Spec and Node manifests. The table is generated from those manifests by
`tools/generate-support-table/generate.py`. Hardware results are listed
separately above.

### What the library handles for you

Each profile derives the report mechanics from its immutable Spec; you still
choose every product and target value.

| Profile | Derived by the library | You specify |
|---|---|---|
| Keyboard | Modifier routing and a full N-Key Rollover bitmap: every declared key has its own bit, so any number of simultaneous keys is reported. | Key Usage range, Report ID, layout, locale, IME behavior. |
| Mouse | Signed 64-bit pending totals for X, Y, Wheel, and AC Pan, split into in-range report fragments; a fragment is consumed only when its transfer completes. | Axis ranges and widths, button count, Wheel/Pan presence, Report ID. |
| Consumer, System, Camera, Telephony | One allow-listed field per press; press/release/tap produce the `1` then `0` sequence each HID control type expects. | Each allowed Usage and its semantics, Report ID. |
| Gamepad | D-pad directions become the eight Hat values plus Null (`15`); adjacent pairs become diagonals, opposite pairs are rejected. Raw four-bit D-pad and no-D-pad modes are also available. | Axes (role, range, width, neutral), buttons, D-pad form, Report ID. |
| Touchscreen / touchpad | Contact Count, multi-report frames, Tip=0 release records with stable Contact IDs, Scan Time, packet sequencing. | Coordinate ranges, maximum contacts, contacts per report, optional fields. |
| Pen (direct and indirect) | Tip/pressure consistency, out-of-range normalization, automatic leave-and-re-enter when switching pen/eraser. | Coordinate and optional-field ranges, hover/eraser/barrel options. |
| Battery Strength | Known and unknown values (Null encoding only when enabled). | Range, width, unknown support, Report ID. |
| Raw Input | Only length, Report ID, and padding are checked against your descriptor. | Descriptor and report bytes. |

A state change is kept until its report is accepted; a conflicting change
before then returns `AOAHID_ERR_BUSY` instead of being merged. See
[PROFILES.md](docs/PROFILES.md) for the exact contract of each profile.

## Build

Requirements: CMake 3.20+, a C++20 compiler, and libusb 1.0.30 or later
(1.0.x only; other major/minor versions are rejected at load time). Shared and
static libraries are selected with explicit switches. Prebuilt archives are on
the [Releases](https://github.com/nemarpuc/libaoahid/releases) page.

```sh
cmake -S . -B build -DAOAHID_BUILD_SHARED=ON -DAOAHID_BUILD_STATIC=ON
cmake --build build --config Release
cmake --install build --prefix staging --config Release
```

For deterministic transport tests, configure with
`-DAOAHID_USE_FAKE_LIBUSB=ON -DAOAHID_BUILD_TESTS=ON`, build, and then run
`ctest --test-dir build -C Release --output-on-failure`. The fake backend is
test code and is never enabled in a release build.

### ThreadSanitizer and low-overhead checks

On Linux with GCC or Clang:

```sh
cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan
```

This builds the library, tests, and examples with ThreadSanitizer against the
fake libusb backend and exercises both event modes. `AOAHID_TSAN=ON` cannot be
combined with `AOAHID_SANITIZE=ON` (ASan+UBSan) or `AOAHID_BUILD_FUZZ=ON`. Use
a normal optimized build for timing.

The test suite also checks that a prewarmed caller-poll keyboard
update/submit/completion cycle performs no C++ heap allocation and takes no
Context or Node mutex. These are host-side checks, not end-to-end USB latency
numbers; see [LATENCY.md](docs/LATENCY.md).

## Use

New to a real device? [QUICKSTART.md](docs/QUICKSTART.md) is a
step-by-step walkthrough covering cables/hubs, Linux udev permissions, the
Windows `adb`-conflict gotcha, and small programs under
[examples/c/verify/](examples/c/verify) built specifically to be watched
(typing text, dragging on the touchscreen, moving the mouse in circles) so you
can confirm a real phone reacts before writing product code.

The complete, fully explicit C session is
[examples/c/multi_profile.c](examples/c/multi_profile.c). For a single profile
in isolation, [examples/c/profiles/](examples/c/profiles) has one self-contained
program per HID profile kind, all nine of which run without a device. In
outline:

1. Create a context and explicitly select caller-poll or internal-thread mode.
2. Discover and select a physical USB device. Discovery sends AOA request 51
   to each device and reports its AOA version; the caller checks for version 2.
   To send no request 51 at all, fill `aoahid_device_info` yourself instead.
3. Open the device in its current USB mode (open sends no AOA request), or
   first switch it with `aoahid_accessory_start`, rediscover the re-enumerated
   accessory-mode device, and open that. The library never waits for
   re-enumeration and never retries; each step is an explicit call.
4. Build immutable specs, register each as its own AOA HID ID, update state,
   then submit reports.
5. Optionally open a Bulk `aoahid_channel` (for example ADB) on the same USB
   handle; HID and Bulk never need a second open of the device.
6. Close nodes/channels/device so neutral state is completed before request 55.

The C header is [include/aoahid.h](include/aoahid.h). The optional C++ header
adds typed node references without changing policy. Python ctypes, C# P/Invoke,
and Rust `-sys` plus typed wrappers live under `bindings/`; they preserve the C
layout and do not choose overrides. The native C API applies the same documented
zero-value transport fallbacks for every language binding.

The device-option fallbacks are 500 ms for control and report transfers, 64
bytes per descriptor fragment, 8 pool slots, a 1024-byte maximum report buffer,
and a 1000 ms close-drain budget. The library never retries a transfer; a
STALLed report stays pending for the caller's resend. These numbers are this
project's choices, not USB/AOA requirements. A timeout is the failure
deadline passed to the backend; it does not add delay to a successful transfer.
Explicit nonzero values remain unchanged. Node reservation `0/0` means no
reservation. Exact device, descriptor, target-parser, and product-profile
policies remain mandatory and are never guessed.

`aoahid_device_open` never switches modes. Devices whose
firmware accepts AOA HID requests only after `ACCESSORY_START` are reached with
`aoahid_accessory_start` instead (requests 51, 52, and 53; request 58 audio is
never sent). There is no call that leaves accessory mode: unplug the device or
use `svc usb setFunctions` through ADB. HID input in current USB mode and
after `aoahid_accessory_start` has been confirmed on hardware. How the AOA
requests work is described in [PROTOCOL.md](docs/PROTOCOL.md).

### ADB on the same device

On Windows a USB device can be opened by only one process, so `adb` and a
libaoahid application cannot both hold the same phone. The companion library
[aoahid_adb_proxy](https://github.com/nemarpuc/aoahid_adb_proxy) runs inside
the application: it opens the phone's ADB interface as a Channel on the
application's own USB handle and serves it on a loopback TCP port, so
`adb connect 127.0.0.1:6555` reaches the phone while HID reports keep using
EP0. Keep the port outside 5555-5585, which the adb server scans for
emulators.

## Releases

A pushed `v*` tag runs the gate before creating a GitHub Release. The release
matrix produces and architecture-checks:

- Linux x86_64 and AArch64 shared objects and static archives;
- Windows x86_64 and ARM64 DLLs, import libraries, and static archives;
- macOS arm64 and x86_64 dynamic libraries and static archives;
- headers, relocatable CMake/pkg-config metadata, notices, checksums, and an
  SPDX SBOM.

For version `X.Y.Z`, the GitHub Release contains exactly 33 uploaded assets:

- twelve archives: `libaoahid-X.Y.Z-linux-{x86_64,aarch64}-ubuntu22.04-{shared,static}.tar.gz`,
  `libaoahid-X.Y.Z-macos-{arm64,x86_64}-{shared,static}.tar.gz`, and
  `libaoahid-X.Y.Z-windows-{x86_64,arm64}-{shared,static}.zip`;
- twelve matching `.spdx.json` sidecars;
- six runtime bundles: the same six target names with `-runtime.tar.gz` on
  Linux and macOS and `-runtime.zip` on Windows;
- the deterministic tagged-tree archive `libaoahid-X.Y.Z-source.tar.gz`;
- `release-manifest.json` and `SHA256SUMS`.

GitHub also displays its automatically generated source-code links separately;
the uploaded `*-source.tar.gz` is the release workflow's byte-validated archive
and is covered by `release-manifest.json` and `SHA256SUMS`.

Linux artifacts have the explicit `ubuntu22.04`/glibc 2.35 baseline. Packaging
inspects every regular shared library and each direct `.so`; the archive,
build metadata, and release manifest record the maximum numeric `GLIBC_*`
requirement, reject every nonnumeric `GLIBC_*` dependency, and reject numeric
requirements above `GLIBC_2.35`.

The complete `*-shared` or `*-static` archive is what you build against: it is
the only asset carrying headers, the import library, and CMake/pkg-config
metadata.

The `*-runtime` bundle is for deploying next to an application that is already
built. It carries the libaoahid shared library, the libusb runtime it loads,
and the full license set for both - `LICENSE`, `NOTICE`,
`THIRD_PARTY_NOTICES.md`, libusb's LGPL-2.1 text, and libusb's official
corresponding source archive. No bare `.so` or `.dll` is published on its own,
because an asset that ships the libusb runtime has to carry libusb's license
and corresponding source in the same distribution unit; a single loose file
cannot. Packaging refuses to assemble a release if any runtime bundle is
missing one of those files.

macOS archives target macOS 11 or later and are built on GitHub-hosted runners
(`macos-15`, `macos-15-intel`). They load the bundled `libusb-1.0.0.dylib`
through `@rpath`, so keep it next to `libaoahid`. The libraries are signed ad
hoc and not notarized, and they have not been run against a phone: the macOS
build is checked with the fake libusb backend only.

Windows release builds use the MSVC dynamic runtime (`/MD`). Deploy the
architecture-matching supported Visual C++ v14 Redistributable as well as the
libusb runtime included next to the DLL in the complete shared archive.

The same workflow always builds, byte-rebuilds, installs, and validates a pure
Python wheel and a managed-only NuGet package before the native GitHub Release
job can publish. Registry publication is disabled unless the repository
variable `AOAHID_PUBLISH_BINDINGS` is exactly `true`. Before enabling it, the
repository owner must configure PyPI Trusted Publishing for the `aoahid`
project/workflow and a scoped `NUGET_API_KEY` secret with authority for the
`AoaHid` package ID; this source tree does not establish registry ownership.
Neither registry package contains native libraries, so users still install a
version-matched shared archive from the GitHub Release.

Workflow failure cannot undo an already-pushed Git tag; it prevents creation of
the GitHub Release and its assets.

## Documentation

| Document | Contents |
|---|---|
| [QUICKSTART.md](docs/QUICKSTART.md) | First run on a real device: cables, permissions, drivers, verification programs |
| [API.md](docs/API.md) | C API reference: objects, options, errors, every function |
| [PROFILES.md](docs/PROFILES.md) | Descriptor and state-machine contract of each profile |
| [EXAMPLES.md](docs/EXAMPLES.md) | Walkthrough of the example programs in C, C++, Python, C#, and Rust |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Object model, threading, send path, source layout |
| [INTERNALS.md](docs/INTERNALS.md) | Implementation notes for contributors: ownership, locks, teardown, descriptor generation, release tooling |
| [PROTOCOL.md](docs/PROTOCOL.md) | How AOA 2.0 HID works and how Android handles the device |
| [LIMITS.md](docs/LIMITS.md) | Numeric limits and validation rules |
| [LATENCY.md](docs/LATENCY.md) | Latency behavior, tuning, and what the tests measure |
| [PORTING.md](docs/PORTING.md) | Platform notes: Linux udev, Windows drivers, macOS, other hosts |
| [TARGET_MATRIX.md](docs/TARGET_MATRIX.md) | Hardware test results |

## License

`libaoahid` itself is **MIT** licensed. The full text is in
[LICENSE](LICENSE); the SPDX identifier is `MIT`. The Python and C# bindings
carry the same MIT license and ship a copy of that file inside their packages.

libusb is a **separate work under LGPL-2.1-or-later**. It is never statically
absorbed into `libaoahid`: every release links it dynamically, including the
archives labelled `static` (that label describes `libaoahid` itself, not
libusb). Each binary archive therefore also carries libusb's own license and
its official source tarball under
`share/doc/libaoahid/third-party/`.

| What you receive | License | Where the text is |
|---|---|---|
| `libaoahid` library, headers, bindings, examples | MIT | `LICENSE` |
| `libusb-1.0` runtime shipped in release archives and runtime bundles | LGPL-2.1-or-later | `share/doc/libaoahid/third-party/libusb-copyright` |
| vcpkg port files bundled in Windows archives | MIT (Microsoft) | `share/doc/libaoahid/third-party/vcpkg-LICENSE.txt` |

Every first-party source file carries its own
`SPDX-License-Identifier: MIT` and copyright line, so a single file stays
identifiable after it is copied out of this repository;
`tools/check_license_headers.py` enforces that in CI. The SPDX sidecar in each
release states the license of every packaged file individually rather than
`NOASSERTION`: first-party output is `MIT`, everything installed from the
libusb dependency is `LGPL-2.1-or-later`, and the pinned vcpkg material is
Microsoft's `MIT`.

Redistributing a release archive unchanged satisfies both licenses as shipped.
If you relink against a modified libusb, the LGPL obligations apply to that
copy. [NOTICE](NOTICE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) record the exact versions and
checksums.

## AI disclosure

Most of the code, tests, and documentation in this repository were written by
an AI coding assistant. Architecture and design decisions, hardware
verification steps, debugging, and review of the AI's output were done by a
human. Hardware results are listed in
[TARGET_MATRIX.md](docs/TARGET_MATRIX.md); review the code before relying on
it for anything security- or safety-relevant.

