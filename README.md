# libaoahid

[Canonical repository](https://github.com/nemarpuc/libaoahid)

Send keyboard, mouse, gamepad, touchscreen, touchpad, pen, and media-key input
from a PC to an Android device over a plain USB cable. The device needs no
root, no app, and no USB debugging: it sees an ordinary USB keyboard or
touchscreen. Windows and Linux, plus macOS builds that are not yet tested with
a device; usable from C, C++, Python, C#, and Rust. MIT licensed.

`libaoahid` is a C++20 library with a stable C ABI. It drives the HID feature
of [Android Open Accessory 2.0](https://source.android.com/docs/core/interaction/accessories/aoa2)
over libusb. You describe each input device (ranges, bit widths, Report IDs,
Usages), starting from initializers that fill in values that work with a
current Android phone; the library builds a valid HID descriptor, registers it
with Android, and keeps the report state machines (multi-touch contacts, key
rollover, pointer deltas) correct. A required value left zero is refused, never
guessed.

Sister library of [`libmimhid`](https://github.com/nemarpuc/libmimhid), which
sends the same kind of input to any USB target (a PC, a console, and so on)
through a microcontroller board, with no Android phone needed. The C ABI and
C++ header follow the same shape, so code moves between the two with few
changes; see [Moving between libaoahid and libmimhid](#moving-between-libaoahid-and-libmimhid).

https://github.com/user-attachments/assets/f6a93ffe-34ea-438b-a09f-d6c55a1f5e23

An introduction to how the library works and why it was built is
[on DEV](https://dev.to/nemarpuc/libaoahid-a-c-abi-library-for-sending-hid-input-to-android-over-usb-1h6f).

## What it is not

- **Not two-way.** Sends Input reports only; nothing comes back from the phone (no keyboard LEDs, rumble, or Feature reports; [PROTOCOL.md](docs/PROTOCOL.md#input-only)).
- **Not for every phone.** Requires AOA version 2 (reported by discovery). Devices and builds beyond [TARGET_MATRIX.md](docs/TARGET_MATRIX.md) have not been tested.
- **Not a scheduler.** Never retries, waits for re-enumeration, or generates key repeat, debounce, or timed actions. Every wait is an explicit call argument.
- **Not a source of defaults.** Infers no product or target value; the header's `*_init` functions fill in one working set, and you change what differs.

## Install

Requirements: a C++20 compiler, CMake 3.20+, and libusb 1.0.30+ in the 1.0 line (other versions are refused at `aoahid_context_create`). On Windows the phone needs a WinUSB-class driver ([PORTING.md](docs/PORTING.md#windows)).

- **Release archive.** Each GitHub [Release](https://github.com/nemarpuc/libaoahid/releases) publishes **exactly 10 uploaded assets**: six binary archives (`libaoahid-X.Y.Z-<platform>-<arch>[-ubuntu22.04].tar.gz` or `.zip`), one SPDX 2.3 SBOM (`libaoahid-X.Y.Z.spdx.json`), the source archive, `release-manifest.json`, and `SHA256SUMS`. Each binary archive carries both shared and static libraries, headers, CMake and pkg-config files, and the libusb runtime ([RELEASES.md](docs/RELEASES.md)).
- **CMake.** `find_package(aoahid CONFIG REQUIRED COMPONENTS shared)` or `COMPONENTS static` (or both), then link `aoahid::aoahid` or `aoahid::aoahid_static`. A `0.1` request is met by a 0.1.x install only.
- **pkg-config.** `pkg-config --cflags --libs aoahid` (shared) or `pkg-config --static --cflags --libs aoahid-static` (static).
- **vcpkg / From source.** The repo's `vcpkg.json` installs libusb for a source build (no `libaoahid` port). `cmake --preset release && cmake --build --preset release` builds against system libusb; `cmake --preset dev` builds and tests against the fake libusb ([BUILDING.md](docs/BUILDING.md)).

## First program

```c
#include <aoahid.h>

int main(void) {
    aoahid_context_options ctx_opts;
    aoahid_device_options dev_opts;
    aoahid_keyboard_options kb_opts;
    aoahid_node_options node_opts;
    aoahid_context_options_init(&ctx_opts);
    aoahid_device_options_init(&dev_opts);
    aoahid_keyboard_options_init(&kb_opts);
    aoahid_node_options_init(&node_opts);

    aoahid_context *ctx = NULL;
    aoahid_discovery *disc = NULL;
    aoahid_device *dev = NULL;
    aoahid_spec *spec = NULL;
    aoahid_node *kb = NULL;

    if (aoahid_context_create(&ctx_opts, &ctx) == AOAHID_OK &&
        aoahid_discover(ctx, AOAHID_CONTROL_TIMEOUT_MS, &disc) == AOAHID_OK &&
        aoahid_discovery_count(disc) > 0 &&
        aoahid_device_open(ctx, aoahid_discovery_get(disc, 0), &dev_opts, &dev) == AOAHID_OK &&
        aoahid_spec_create_keyboard(&kb_opts, &spec) == AOAHID_OK &&
        aoahid_node_open(dev, spec, &node_opts, AOAHID_CONTROL_TIMEOUT_MS, &kb) == AOAHID_OK) {
        /* Wait ~100 ms in real code so Android's input reader attaches first. */
        aoahid_keyboard_key(kb, 0x04, 1); /* Keyboard a and A */
        aoahid_node_submit_blocking(kb, AOAHID_SEND_TIMEOUT_MS);
        aoahid_keyboard_key(kb, 0x04, 0);
        aoahid_node_submit_blocking(kb, AOAHID_SEND_TIMEOUT_MS);
    }

    if (kb != NULL) aoahid_node_close(kb, AOAHID_CONTROL_TIMEOUT_MS, AOAHID_DRAIN_TIMEOUT_MS);
    if (spec != NULL) aoahid_spec_release(spec);
    if (dev != NULL) aoahid_device_close(dev, AOAHID_CONTROL_TIMEOUT_MS, AOAHID_DRAIN_TIMEOUT_MS);
    if (disc != NULL) aoahid_discovery_destroy(disc);
    if (ctx != NULL) aoahid_context_destroy_blocking(ctx, AOAHID_CONTROL_TIMEOUT_MS,
                                                     AOAHID_DRAIN_TIMEOUT_MS, AOAHID_DRAIN_TIMEOUT_MS);
    return 0;
}
```

[QUICKSTART.md](docs/QUICKSTART.md) takes you from a fresh checkout to a phone
reacting to the verification programs under `examples/c/verify/`.
[EXAMPLES.md](docs/EXAMPLES.md) walks through every C, C++, Python, C#, and
Rust example (`examples/c/multi_profile.c`, `include/aoahid.hpp`).

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
| Digitizer | conditional | yes | no | no | Target classification and mapping evidence required |
| Toggle: Camera keys | conditional | yes | no | no | HUT Camera Auto-focus/Shutter subset only |
| Toggle: Telephony keys | conditional | yes | no | no | Caller allow-list and target mapping required |
| Battery report (shown on a Keyboard) | conditional | yes | no | no | Second Input report inside any generated profile; Linux registers a battery only with an input device |
| Validated raw descriptor | unknown | yes | no | no | Explicit no-Android-support acknowledgement |
<!-- profile-table:end -->

What each profile derives for you and what you specify is in [PROFILES.md](docs/PROFILES.md).

## Moving between libaoahid and libmimhid

Both libraries share the same profile `Spec` factories, report state machines,
and error model (`s/aoahid/mimhid/` for profile and mutation calls). They differ
where the hardware differs: `libaoahid` drives an Android phone over `libusb`
with a `Context` handle (`aoahid_context_poll` or internal event thread), AOA 2.0
control requests `54`–`57`, and optional Bulk `Channel`s, whereas `libmimhid`
drives an MCU board over a host serial port with full USB/endpoint descriptors,
`mimhid_commit_timeouts`, and two-way HID callbacks. The full comparison is in
[API.md](docs/API.md#differences-from-libmimhid).

## Verification status

- Hardware: touchscreen, touchpad, keyboard, mouse, gamepad (Hat Switch, raw D-pad buttons, and no D-pad), Consumer Control media keys, pen, and digitizer took effect on a Samsung Galaxy Tab S11 and a POCO F6 Pro (pen, digitizer, and touchpad on the Tab S11 under Arch Linux only), from Windows 10 x64 and Arch Linux, in 2026-09 and 2026-10.
- Battery report: `BATTERY` device class registration via `dumpsys input` was confirmed on the Samsung Galaxy Tab S11 (2026-10-11); live percentage reporting to the Android UI is not yet verified.
- Release 0.1.0 itself is not hardware-verified: the API around the verified transport and profiles has changed since ([CHANGELOG.md](CHANGELOG.md)).
- Not tested on hardware: raw reports, the System Control, Camera, and Telephony toggles, live battery percentage reporting, and macOS.
- Every unit and `sim` test runs in CI against a deterministic fake libusb, with ASan, UBSan, and ThreadSanitizer builds and an 87% line / 75% branch coverage floor ([VERIFICATION.md](docs/VERIFICATION.md)).

## ADB on the same phone

On Windows only one process can open a USB device. The companion library
[aoahid_adb_proxy](https://github.com/nemarpuc/aoahid_adb_proxy) opens the
phone's ADB interface as a Channel ([API.md](docs/API.md#bulk-channels)) and
serves it on a loopback TCP port (`adb connect 127.0.0.1:6555`, outside the
5555–5585 emulator scan range); otherwise run `adb kill-server` first
([QUICKSTART.md](docs/QUICKSTART.md#4-windows-adb-or-a-manufacturer-driver-can-block-you)).

## Documentation

| Document | Contents |
|---|---|
| [QUICKSTART.md](docs/QUICKSTART.md) | First run on a real device: cables, permissions, drivers, verification programs |
| [BUILDING.md](docs/BUILDING.md) | Requirements, CMake options and presets, tests, sanitizers, coverage, using the installed package |
| [API.md](docs/API.md) | C API reference: objects, options, errors, every function |
| [PROFILES.md](docs/PROFILES.md) | Descriptor and state-machine contract of each profile |
| [EXAMPLES.md](docs/EXAMPLES.md) | Walkthrough of the example programs in C, C++, Python, C#, and Rust |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Object model, threading, send path, design decisions |
| [INTERNALS.md](docs/INTERNALS.md) | For contributors: ownership, locks, teardown, descriptor generation, release tooling |
| [PROTOCOL.md](docs/PROTOCOL.md) | How AOA 2.0 HID works and how Android handles the device |
| [LIMITS.md](docs/LIMITS.md) | Numeric limits, validation rules, and known platform differences |
| [LATENCY.md](docs/LATENCY.md) | Latency behavior, what the tests show, and how to measure |
| [PORTING.md](docs/PORTING.md) | Platform notes: Linux udev, Windows drivers, macOS, other hosts |
| [TARGET_MATRIX.md](docs/TARGET_MATRIX.md) | Hardware test results per device, host and profile |
| [VERIFICATION.md](docs/VERIFICATION.md) | What is verified, in which layer, and how to reproduce it |
| [RELEASES.md](docs/RELEASES.md) | Release assets, archive contents, platform baselines, licenses |
| [CHANGELOG.md](CHANGELOG.md) | Changes per release, with the hardware-verification status of each |

## License

`libaoahid`, its headers, bindings and examples are MIT licensed ([LICENSE](LICENSE)).
libusb is a separate work under LGPL-2.1-or-later that every release links
dynamically, including the static `libaoahid`; each binary archive carries
libusb's license and official source ([RELEASES.md](docs/RELEASES.md#licenses)).

## AI disclosure

Most of the code, tests, and documentation in this repository were written by
an AI coding assistant. Architecture and design decisions, hardware
verification steps, debugging, and review of the AI's output were done by a
human. Hardware results are listed in [TARGET_MATRIX.md](docs/TARGET_MATRIX.md);
review the code before relying on it for anything security- or safety-relevant.
