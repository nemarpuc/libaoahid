# libaoahid

[Canonical repository](https://github.com/nemarpuc/libaoahid)

Send keyboard, mouse, gamepad, touchscreen, touchpad, pen, and media-key input
from a PC to an Android device over a plain USB cable. The device needs no
root, no app, and no USB debugging: it sees an ordinary USB keyboard or
touchscreen. Builds for Linux, Windows, and macOS; so far tested on a phone
only from Linux ([Verification status](#verification-status)). Usable from C,
C++, Python, C#, and Rust. MIT licensed.

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
- **Not a guesser.** Infers no product or target value and refuses a required value left zero. The header's `*_init` functions and `AOAHID_*_TIMEOUT_MS` constants do fill in one working set of values, which you start from and change.

## Install

Requirements: a C++20 compiler, CMake 3.20+, and libusb 1.0.30+ in the 1.0 line (other versions are refused at `aoahid_context_create`). On Windows the phone needs a WinUSB-class driver ([PORTING.md](docs/PORTING.md#windows)).

- **Release archive.** Each GitHub [Release](https://github.com/nemarpuc/libaoahid/releases) publishes **exactly 10 uploaded assets**: six binary archives (`libaoahid-X.Y.Z-<platform>-<arch>[-ubuntu22.04].tar.gz` or `.zip`), one SPDX 2.3 SBOM (`libaoahid-X.Y.Z.spdx.json`), the source archive, `release-manifest.json`, and `SHA256SUMS`. Each binary archive carries both shared and static libraries, headers, CMake and pkg-config files, and the libusb runtime ([RELEASES.md](docs/RELEASES.md)).
- **CMake.** `find_package(aoahid CONFIG REQUIRED COMPONENTS shared)` or `COMPONENTS static` (or both), then link `aoahid::aoahid` or `aoahid::aoahid_static`. A `0.1` request is met by a 0.1.x install only.
- **pkg-config.** `pkg-config --cflags --libs aoahid` (shared) or `pkg-config --static --cflags --libs aoahid-static` (static).
- **vcpkg / From source.** The repo's `vcpkg.json` installs libusb for a source build (no `libaoahid` port). `cmake --preset release && cmake --build --preset release` builds against system libusb; `cmake --preset dev` builds and tests against the fake libusb ([BUILDING.md](docs/BUILDING.md)).

## First program

It types one `a` on the first device that reports AOA version 2 or later.
Every call is checked, and it waits 100 ms after `aoahid_node_open`, because
Android registers the new HID after the last descriptor fragment without
telling the host ([API.md](docs/API.md#open-sequence)).

```c
#define _POSIX_C_SOURCE 200809L /* nanosleep */
#include <aoahid.h>
#include <stdio.h>
#if defined(_WIN32)
#include <windows.h>
static void sleep_ms(unsigned ms) { Sleep(ms); }
#else
#include <time.h>
static void sleep_ms(unsigned ms) {
    struct timespec t = {(time_t)(ms / 1000U), (long)(ms % 1000U) * 1000000L};
    nanosleep(&t, NULL);
}
#endif

static int failed(const char* step, aoahid_result result) {
    /* Read the record first: every other call, aoahid_result_name too, replaces it. */
    const aoahid_error_detail* detail = aoahid_last_error();
    fprintf(stderr, "%s failed (%s: %s): ", step, detail->field ? detail->field : "-",
            detail->reason ? detail->reason : "-");
    fprintf(stderr, "%s\n", aoahid_result_name(result));
    return 1;
}

int main(void) {
    aoahid_context_options ctx_opts;
    aoahid_device_options dev_opts;
    aoahid_keyboard_options kb_opts;
    aoahid_node_options node_opts;
    aoahid_context_options_init(&ctx_opts);
    aoahid_device_options_init(&dev_opts);
    aoahid_keyboard_options_init(&kb_opts);
    aoahid_node_options_init(&node_opts);

    aoahid_context* ctx = NULL;
    aoahid_discovery* disc = NULL;
    aoahid_device* dev = NULL;
    aoahid_spec* spec = NULL;
    aoahid_node* kb = NULL;
    const aoahid_device_info* phone = NULL;
    aoahid_result r;
    int status = 1;

    if ((r = aoahid_context_create(&ctx_opts, &ctx)) != AOAHID_OK) return failed("context", r);
    if ((r = aoahid_discover(ctx, AOAHID_CONTROL_TIMEOUT_MS, &disc)) != AOAHID_OK) {
        failed("discover", r);
        goto done;
    }
    /* Discovery lists every device that answered request 51; HID needs version 2. */
    for (size_t i = 0; i < aoahid_discovery_count(disc) && phone == NULL; ++i) {
        const aoahid_device_info* info = aoahid_discovery_get(disc, i);
        if (info != NULL && info->protocol_version >= 2) phone = info;
    }
    if (phone == NULL) {
        fprintf(stderr, "no device with AOA version 2 or later\n");
        goto done;
    }
    if ((r = aoahid_device_open(ctx, phone, &dev_opts, &dev)) != AOAHID_OK) {
        failed("device", r);
        goto done;
    }
    if ((r = aoahid_spec_create_keyboard(&kb_opts, &spec)) != AOAHID_OK) {
        failed("spec", r);
        goto done;
    }
    if ((r = aoahid_node_open(dev, spec, &node_opts, AOAHID_CONTROL_TIMEOUT_MS, &kb)) != AOAHID_OK) {
        failed("node", r);
        goto done;
    }
    /* Android registers the HID after the last descriptor fragment without telling the host;
     * a report sent at once can STALL or be lost (docs/API.md, "Open sequence"). */
    sleep_ms(100);

    if ((r = aoahid_keyboard_key(kb, 0x04, 1)) != AOAHID_OK || /* Keyboard a and A */
        (r = aoahid_node_submit_blocking(kb, AOAHID_SEND_TIMEOUT_MS)) != AOAHID_OK ||
        (r = aoahid_keyboard_key(kb, 0x04, 0)) != AOAHID_OK ||
        (r = aoahid_node_submit_blocking(kb, AOAHID_SEND_TIMEOUT_MS)) != AOAHID_OK) {
        failed("key", r);
        goto done;
    }
    status = 0;

done:
    if (kb != NULL) {
        r = aoahid_node_close(kb, AOAHID_CONTROL_TIMEOUT_MS, AOAHID_DRAIN_TIMEOUT_MS);
        /* AOAHID_CLOSE_PENDING leaves the Node open; aoahid_device_close below closes it. */
        if (r != AOAHID_OK && r != AOAHID_CLOSE_PENDING) status = failed("node close", r);
    }
    if (spec != NULL) aoahid_spec_release(spec);
    if (dev != NULL && (r = aoahid_device_close(dev, AOAHID_CONTROL_TIMEOUT_MS,
                                                AOAHID_DRAIN_TIMEOUT_MS)) != AOAHID_OK) {
        status = failed("device close", r);
    }
    if (disc != NULL) aoahid_discovery_destroy(disc);
    if ((r = aoahid_context_destroy_blocking(ctx, AOAHID_CONTROL_TIMEOUT_MS,
                                             AOAHID_DRAIN_TIMEOUT_MS,
                                             AOAHID_DRAIN_TIMEOUT_MS)) != AOAHID_OK) {
        status = failed("context destroy", r);
    }
    return status;
}
```

Build it against an installed package or an unpacked release archive:

```sh
cc first.c $(pkg-config --cflags --libs aoahid) -o first
```

or, in CMake, `find_package(aoahid CONFIG REQUIRED COMPONENTS shared)` and
`target_link_libraries(first PRIVATE aoahid::aoahid)`. With an archive, point
`PKG_CONFIG_PATH` at its `lib/pkgconfig`, and the loader at its `lib/` when
you run it.

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

The two headers have the same shape: options structures that start with
`struct_size`, one close rule, every timeout an argument, and the same profile
factories and input calls (`aoahid_keyboard_key` is `mimhid_keyboard_key`).
The transport differs: here a Context drives libusb (`aoahid_context_poll` or
an internal event thread), AOA requests 51 to 57 register and feed the HIDs,
and Bulk `Channel`s reach other interfaces; `libmimhid` drives a board over a
serial port, uploads full USB descriptors, and carries two-way HID. Three
things behave differently under the same names:

- **`*_init`.** Here the initializers fill in working values, and
  `AOAHID_CONTROL_TIMEOUT_MS`, `AOAHID_SEND_TIMEOUT_MS`, and
  `AOAHID_DRAIN_TIMEOUT_MS` are working timeouts. libmimhid's `*_init` only
  zeroes the structure and sets `struct_size`, and a required field left zero
  is `MIMHID_ERR_UNSET_FIELD`, so code moved there must set every value itself.
- **`AOAHID_CLOSE_PENDING` (15).** A close here can return it while a USB
  transfer is still in flight, and the handle stays yours. libmimhid has no
  such result (15 is unused there): its writes finish inside the call.
- **Result 4.** Here it is `AOAHID_ERR_NOT_AOA` (the device does not speak
  AOA); in libmimhid it is `MIMHID_ERR_PROTOCOL` (the board's firmware has
  another version).

The full comparison is in [API.md](docs/API.md#differences-from-libmimhid).

## Verification status

- Hardware: the transport and the Keyboard, Mouse, Toggle, Gamepad,
  Touchscreen, Touchpad, Pen, and Digitizer profiles were verified on a
  Samsung Galaxy Tab S11 from Arch Linux, with `devtest` and kernel `getevent`
  ([CHANGELOG.md](CHANGELOG.md)).
- Release 0.1.0 itself is not hardware-verified: the API around that transport
  and those profiles has changed since.
- Every unit and `sim` test runs in CI against a deterministic fake libusb,
  with ASan, UBSan, and ThreadSanitizer builds and an 87% line / 75% branch
  coverage floor ([VERIFICATION.md](docs/VERIFICATION.md)).

## ADB on the same phone

On Windows only one process can open a USB device
([PROTOCOL.md](docs/PROTOCOL.md#host-side-caveats)). The companion library
[aoahid_adb_proxy](https://github.com/nemarpuc/aoahid_adb_proxy) opens the
phone's ADB interface as a Channel ([API.md](docs/API.md#bulk-channels)) and
serves it on a loopback TCP port (`adb connect 127.0.0.1:6555`, outside the
odd ports 5555 to 5585 that the adb server polls for emulators,
`PollAllLocalPortsForEmulator` in AOSP
[`client/transport_emulator.cpp`](https://android.googlesource.com/platform/packages/modules/adb/+/1cf2f017d312f73b3dc53bda85ef2610e35a80e9/client/transport_emulator.cpp),
retrieved 2026-11-10); otherwise run `adb kill-server` first
([QUICKSTART.md](docs/QUICKSTART.md#quickstart-windows)).

## Documentation

| Document | Contents |
|---|---|
| [QUICKSTART.md](docs/QUICKSTART.md) | First run on a real device: cables, permissions, drivers, verification programs |
| [BUILDING.md](docs/BUILDING.md) | Requirements, CMake options and presets, tests, sanitizers, coverage, using the installed package |
| [API.md](docs/API.md) | C API guide and a reference entry for each of the 53 functions |
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
