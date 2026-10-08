# Changelog

All notable changes to libaoahid are recorded here. This project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [4.2.0] - 2026-10-09

No change to the C ABI. The C++ header `aoahid.hpp` was replaced and is not
source compatible with 4.1.0. It was not hardware-verified again.

### Changed

- `aoahid.hpp` now forwards every function of `aoahid.h` unchanged under
  namespace `aoahid`, without the `aoahid_` prefix (`aoahid_kbd` is
  `aoahid::kbd`). It no longer covers just some of the node calls.

### Removed

- The typed node references (`aoa::node_ref`, `aoa::keyboard_node_ref`, ...,
  `aoa::raw_node_ref`) and `aoa::bind`. Call the `aoahid::` functions on an
  `aoahid_node*` instead.

## [4.1.0] - 2026-10-04

No public API or ABI change. It was not hardware-verified again: the macOS
archives were not run against a phone, and the `Verified` rows in
`docs/TARGET_MATRIX.md` are still the 4.0.2 results.

### Added

- Release archives for macOS arm64 and x86_64: `libaoahid-X.Y.Z-macos-{arm64,x86_64}-{shared,static}.tar.gz`,
  their SPDX sidecars, and `-runtime.tar.gz` bundles. They bundle libusb 1.0.30
  built from its unmodified source, target macOS 11, load libusb through
  `@rpath`, and are signed ad hoc, not notarized. The release now has 33
  assets instead of 23. They are checked with the fake libusb backend and
  installed-package consumers only.

## [4.0.5] - 2026-10-04

No public API or ABI change. It was not hardware-verified again: it was checked
with the host test suite only, so the `Verified` rows in
`docs/TARGET_MATRIX.md` are still the 4.0.2 results.

### Changed

- Two changes from 4.0.4 are taken out again, and the code they touched is
  that of 4.0.3. The atomic flag that let `aoahid_node_submit` skip the
  Context mutex saved one uncontended lock per report, which was never
  measured, and had to be kept in step with the error latch by hand. The
  wake-up after a Device is handed to the Context graveyard covered a window
  of microseconds whose only cost is a later reclaim.
- The field-width fix from 4.0.4 stays: every combination of widths from 1 to
  32 bits is accepted.

## [4.0.4] - 2026-10-04

No public API or ABI change. It was not hardware-verified again: it was checked
with the host test suite only (including the ThreadSanitizer build), so the
`Verified` rows in `docs/TARGET_MATRIX.md` are still the 4.0.2 results.

### Fixed

- Mouse, Pen, Touchscreen, and Touchpad Specs no longer reject width
  combinations that put a field across a fifth byte: a 32-bit Y behind a
  31-bit X, or an X of 29 bits or more behind a 4-bit Contact Identifier. They
  returned `AOAHID_ERR_PARAM` ("spec.field"). The generator now pads to the next
  byte boundary in exactly that case. Every descriptor that was accepted
  before is byte-identical; the golden descriptors are unchanged.
- In internal-thread mode, a Device that `aoahid_device_close` hands to the
  Context graveyard is reclaimed at once. If its last callback finished
  between the close's final check and the hand-over, nothing woke the event
  thread, and the USB handle stayed open until the next USB event or the
  60-second idle bound.

### Changed

- `aoahid_node_submit` and `aoahid_raw_submit` in internal-thread mode read
  one atomic flag instead of locking the Context mutex to find that no
  deferred error is latched.

### Documentation

- `aoahid_spec_create_toggle` in `aoahid.h`: a Camera Control Usage outside
  Auto-focus/Shutter returns `AOAHID_ERR_PARAM`, as the code always did. The
  comment said `AOAHID_ERR_UNSUPPORTED`.
- `docs/LIMITS.md`: a Logical or Physical Maximum is written unsigned when
  both extents are non-negative, and the conditional padding above.
- Added `docs/INTERNALS.md`: lifetimes, locks, the send and completion paths,
  the Channel, the descriptor generator, and the test and release tooling,
  with source references.

## [4.0.3] - 2026-10-03

No public API, ABI, or behavior change. A maintenance release. It was not
hardware-verified again: it was checked with the host test suite only, so the
`Verified` rows in `docs/TARGET_MATRIX.md` are still the 4.0.2 results.

### Changed

- The raw descriptor validator is split by HID item type (Global, Local, Main,
  Input) and names the item tags instead of using bare numbers. The old and
  new validators returned identical results, messages, and offsets for three
  million generated descriptors.
- Touchscreen and touchpad diagnostics take their field names from string
  literals. They were formatted into one thread-local buffer, so an
  `aoahid_error_detail` copied before a later touch Spec call on the same
  thread could read a different name.

### Tests

- `aoahid_tests` runs only the suites named on its command line
  (`item_writer`, `profiles`, `transport`, `identity`); with no argument it
  runs them all, as before.

## [4.0.2] - 2026-09-30

No public API or ABI change. Hardware-verified on a Samsung Galaxy Tab S11 and a
POCO F6 Pro (Windows 10 x64 and Arch Linux): the profiles and paths marked
`Verified` in `docs/TARGET_MATRIX.md`; every other row is still unverified.

### Fixed

- `aoahid_node_submit_blocking` in internal-thread mode no longer spins when
  every transfer pool slot belongs to other Nodes. It waits for a Device-level
  signal that each report completion raises, sends as soon as a slot is free,
  and uses no CPU while waiting. A regression test covers it.
- The STALL completion message no longer claims first-report attempts. It says
  the state stays pending for a resend and names the usual cause right after
  `aoahid_node_open` (Android has not finished registering the HID). The
  NO_DEVICE message now covers the case before the request was sent.

### Documentation

- Added `docs/ARCHITECTURE.md` and `docs/PROTOCOL.md`, verified against the code
  and official sources.
- Rewrote README, `docs/API.md`, `docs/PROFILES.md`, `docs/LIMITS.md`,
  `docs/LATENCY.md`, `docs/PORTING.md`, `docs/QUICKSTART.md`,
  `docs/EXAMPLES.md`, `docs/TARGET_MATRIX.md`, `examples/README.md`, and
  `tools/device-check/README.md`, and removed references to internal working
  notes from them and from source comments. Hardware results for the
  touchscreen, keyboard, mouse, gamepad, and media-key profiles are recorded.
- `docs/API.md`: exact STALL resend behavior for `aoahid_node_submit`,
  `aoahid_node_submit_blocking`, and Raw Nodes; `aoahid_node_close` may return
  an earlier report's error. `docs/TARGET_MATRIX.md`: the registration-race
  STALL is confirmed on hardware.
- `aoahid_node_open` notes that Android registers the HID asynchronously, so
  the first input needs a short wait. `aoahid_device_options.send_timeout_ms`
  notes that contacts lifted in a timed-out touch report stay occupied until
  the Node is reopened. The `AOAHID_ERR_BUSY` text of the profile calls also
  covers an unsent opposite edge.
- State that accessory mode is not required for HID, and fix a Doxygen link to
  the README in `docs/PROTOCOL.md`.
- README: link to the introduction article on DEV.

### Examples and tooling

- `verify_mouse` and `verify_touch` wait 100 ms after `aoahid_node_open`.
- The release, license-header, and provenance checks and the Doxygen
  configuration no longer refer to the removed working notes.

## [4.0.1] - 2026-09-29

No library code changed. Hardware-verified on a Samsung Galaxy Tab S11 and a
POCO F6 Pro (Windows 10 x64 and Arch Linux): the profiles and paths marked
`Verified` in `docs/TARGET_MATRIX.md`; every other row is still unverified.

### Documentation

- README: a Demo section with a video of aoahid_player, an app built on this
  library.
- `docs/TARGET_MATRIX.md`: rows 1, 2, 3 and 6 of the accessory mode and Bulk
  Channel table are verified on hardware (Samsung Galaxy Tab S11 and POCO F6
  Pro, on Windows 10 x64 and Arch Linux).
- `docs/TARGET_MATRIX.md`: Keyboard, Mouse, Consumer Toggle, all three
  Gamepad D-pad forms, and fixed-MT Touchscreen took effect on the same
  hardware (Application API column).
- `docs/PORTING.md`: the Samsung-on-Windows driver note is a hardware
  observation, not a user report.

## [4.0.0] - 2026-09-28

### Changed (breaking)

- `aoahid_device_open` sends no AOA request. It no longer sends request 51 to
  re-check the version, so it no longer returns `AOAHID_ERR_NOT_AOA` or a
  version error. A device without AOA 2.0 HID opens and then fails at
  `aoahid_node_open` with `AOAHID_ERR_STALL`.
- The library applies no AOA version policy. `aoahid_discover` still sends
  request 51 to each device and lists those with a nonzero version; the new
  `aoahid_device_info.protocol_version` reports that version, and the caller
  decides what to open. A caller that fills `aoahid_device_info` itself sends
  no request 51 at all, which also avoids Android's accessory-handshake
  broadcast.
- `aoahid_device_info` gains `protocol_version`. It fits in existing padding,
  so the size and the offsets of `serial` and `product` are unchanged.
- `aoahid_device_options` drops every member that did nothing:
  `startup_mode`, `accept_future_protocol_versions`,
  `reenumeration_timeout_ms`, `first_report_attempts`,
  `first_report_backoff_us`, `accessory_strings`, and
  `enable_deprecated_audio_mode`. `validate_reports` moves to the end so the
  structure has no interior padding. Rebuild every caller against this header.
- Removed: `aoahid_device_protocol_version()`, `aoahid_startup_mode`,
  `AOAHID_START_CURRENT_USB_MODE`, `AOAHID_START_ACCESSORY_MODE`, and
  `AOAHID_ERR_VERSION` (code 5 is now unassigned; no other code changes). Use
  `aoahid_accessory_start` to switch a device to accessory mode.
- The shared library is `libaoahid.so.4` with the `AOAHID_4.0` symbol node.
- The Python, C#, and Rust bindings follow the new layouts and drop the
  removed names. The C#, Python, Rust, and C examples print each device's AOA
  version and refuse a version below 2 themselves.

### Documentation

- The guide, API, design, and example documents describe probe-free open and
  caller-side version checks, and no longer describe ABI placeholders. The
  guide's first-report policy now matches the library, which has never retried
  since 2.0.0.

### Verification status

Fake-backend tests only (the dev, TSan, and ASan/UBSan configurations).
Nothing is hardware-verified.

## [3.0.5] - 2026-09-28

### Changed

- Reports are serialized a byte at a time instead of a bit at a time, about
  2.9 times faster for a 16-contact touchscreen report (766 ns to 267 ns on
  one x86-64 machine). The bytes on the wire are unchanged; the new encoder
  was checked against the old one over 2,000,000 random fields.
- Internal cleanups with no behavior change: unused parameters, a duplicated
  protocol-version check, and setup-packet fields rewritten on every submit
  although acquiring the transfer slot had already written them.

### Fixed

- `examples/c/verify`: a failure could print "no field" and "no reason"
  instead of the failing call's diagnostic, because `aoahid_result_name()`
  clears the last-error record and C leaves argument evaluation order
  unspecified. The record is now read first.
- On Linux, the shared library now relinks when `cmake/aoahid.map` changes,
  as the Apple build already did for its export list.
- The integration tests compile with Clang 22: the replacement
  `operator delete` overloads in `tests/integration/test_transport.cpp` no
  longer take top-level `const` parameters.

### Documentation

- README: the ADB section no longer suggests `adb connect localhost:5555`, a
  port the adb server scans for emulators, and describes how
  aoahid_adb_proxy works (a Channel on the application's own USB handle).
- QUICKSTART: `AOAHID_USE_FAKE_LIBUSB` is `OFF` unless a test configuration
  turns it on, and `aoahid_verify_all` is listed with the other verify
  programs. `examples/README.md` lists `touchpad.c`.

### Verification status

Fake-backend tests only (the dev, TSan, and ASan/UBSan configurations, and a
Clang build). Nothing is hardware-verified.

## [3.0.4] - 2026-09-27

### Changed

- The repository is renamed from `nemarpuc/Libaoa_hid` to
  `nemarpuc/libaoahid`, matching the library (`libaoahid`), header
  (`aoahid.h`), and CMake package (`aoahid`) names. Package metadata, release
  validation (`AOAHID_EXPECTED_REPOSITORY`), and documentation links use the
  new name. GitHub redirects the old URLs; the Pages URL is now
  `https://nemarpuc.github.io/libaoahid/`. The library is unchanged.
- 3.0.3 was tagged, but its release did not publish because the repository
  was renamed while the release job ran. 3.0.4 contains all of 3.0.3.

### Verification status

No library code changed. Nothing is hardware-verified.

## [3.0.3] - 2026-09-27

### Changed

- The C verification examples' "no USB device found" message names the
  platform's usual cause: the udev rule on Linux, and a driver libusb can open
  (such as WinUSB) on Windows. Before, every platform got the same text.
- `docs/PORTING.md` and `docs/QUICKSTART.md`: the Windows section no longer
  says the Google USB Driver is non-WinUSB (it is WinUSB-based). It now names
  both causes, an `adb` server holding the interface and a manufacturer
  driver instead of WinUSB, with the Samsung (`dg_ssudbus`) report as an
  example.

### Verification status

No library code changed. Nothing is hardware-verified.

## [3.0.2] - 2026-09-27

### Fixed

- On Windows, a Channel on the interface that libusb's WinUSB backend
  auto-claims for control transfers (typically ADB) could lose its claim when
  a HID report transfer in flight during `aoahid_channel_open` completed.
  `aoahid_channel_open` now waits, within the Device's `close_drain_timeout_ms`,
  until no report transfer is in flight before it claims the interface, and
  returns `AOAHID_ERR_TIMEOUT` if one still is. `aoahid_channel_close` waits
  the same way (best effort) before releasing it.

### Verification status

Fake-backend tests only. Nothing is hardware-verified.

## [3.0.1] - 2026-09-27

### Packaging

- Linux archives record how the bundled libusb was built from the bundled
  source (`libusb_build` in `share/doc/libaoahid/build-metadata.json`), and
  `THIRD_PARTY_NOTICES.md` points to it. The library itself is unchanged.

### Verification status

No library code changed. Nothing is hardware-verified.

## [3.0.0] - 2026-09-24

### Added

- `aoahid_channel_options.read_mode` and `aoahid_channel_read_mode`.
  `AOAHID_CHANNEL_READ_STREAM` (zero) keeps the read-ahead byte stream.
  `AOAHID_CHANNEL_READ_REQUEST` submits no read-ahead. Instead, a read with
  nothing buffered submits one IN transfer of its capacity, rounded up to
  `wMaxPacketSize` and capped at `transfer_bytes`, the way host adb reads a
  length-prefixed payload. A request completes as soon as it is full, so a
  packet-aligned message with no zero-length packet no longer waits for more
  data. A timeout leaves the request pending for the next read, so no byte is
  lost.
- The Python, C#, and Rust bindings expose the new field and constants.

### Changed (breaking)

- `aoahid_channel_options` grows by one 32-bit field, so its `struct_size`
  changes. Rebuild every caller against this header. Zero keeps the 2.x
  behavior.

### Documentation

- `docs/API.md` explains when a stream-mode read waits (a Bulk IN transfer
  completes only when full or on a short packet), with AOSP adb references.

### Verification status

Verified with fake-libusb tests (request sizing, pending timeout, buffered
remainder, zero-length packet, unplug), plus ASan/UBSan and ThreadSanitizer.
Nothing is hardware-verified.

## [2.0.1] - 2026-09-23

### Removed

- An outdated design note describing a rejected automatic design and an
  unimplemented ADB-server plan. `docs/API.md` now cites the
  Android 10-second accessory-request timeout directly from AOSP
  `UsbDeviceManager.java`.
- An unused internal Channel query and two fake-libusb test helpers left over
  from that rejected design. No public function, structure, value, or behavior
  changes; the ABI is identical to 2.0.0.

### Verification status

Documentation and dead-code removal only. Nothing is hardware-verified; the
accessory-mode and Channel hypotheses in `docs/TARGET_MATRIX.md` remain
**[unverified on hardware]**, and every profile remains not hardware-verified.

## [2.0.0] - 2026-09-23

### Changed (breaking)

- The library never retries a transfer. The first report after
  `aoahid_node_open` used to be resent automatically after a STALL (default 20
  attempts, 1 ms apart); it is now sent once and `AOAHID_ERR_STALL` is returned.
  A STALL means the target refused request 57 (`f_accessory` stalls while the
  HID ID is not yet registered), so the refused state now stays pending and the
  caller's next submit resends it; timeouts, cancellations, and I/O errors still
  consume the submitted state because delivery is unknown.
  `first_report_attempts` and `first_report_backoff_us` stay in
  `aoahid_device_options` for layout compatibility and are ignored.
  `examples/c/verify/verify_common.h` shows a bounded caller-side resend.
- SOVERSION 2 (`libaoahid.so.2`) with the ELF symbol-version node
  `AOAHID_2.0`, because the change above alters documented behavior.

### Added

- `aoahid_accessory_start` with `aoahid_accessory_options`: sends AOA requests
  51, 52 (the caller's `aoahid_aoa_strings`; a nonempty manufacturer and model
  are required, each at most 256 bytes including its NUL as AOA 1.0 specifies,
  checked before any request), and 53 to a device in its current USB mode, then
  closes it and returns. It never waits for re-enumeration and never retries. The
  application rediscovers the accessory-mode device (VID `0x18D1`, PID
  `0x2D00`-`0x2D05`) and opens it with the unchanged `aoahid_device_open`. A
  device already in accessory mode receives no request. There is no call that
  leaves accessory mode.
- Bulk Channels on a Device: `aoahid_channel_open`, `aoahid_channel_read`,
  `aoahid_channel_write`, `aoahid_channel_close`, and `aoahid_channel_options`.
  A Channel selects configuration 1 only on an unconfigured device (AOA 1.0),
  claims the first interface with the requested class triple (for example ADB
  `0xFF/0x42/0x01`) on the Device's own USB handle, keeps IN
  transfers submitted ahead of the reader, uses a transfer pool separate from
  HID, reads as a byte stream, back-pressures writes, and can end a write with
  an explicit zero-length packet that behaves the same on every backend. Read
  and write take no lock and allocate nothing. Channels follow the Node close
  rules, and `aoahid_device_close` also closes them.
- `examples/c/verify/verify_accessory.c`: switches the first phone to
  accessory mode, types one letter, and opens an ADB Channel when USB debugging
  is on.
- C, Python, C#, and Rust declarations for every new function and structure.

### Changed

- Every USB handle is owned by one internal Port shared by the Device and its
  Channels. `aoahid_discover` probes a device that an open Device already holds
  through that handle instead of opening it a second time (WinUSB refuses a
  second open). `aoahid_accessory_start` likewise borrows an open Device's
  handle.
- `docs/API.md` now describes the `aoahid_toggle` release rule introduced in
  1.0.0 (`usage` must be `0` or the pressed Usage).
- `udev/51-aoahid.rules` comments describe the accessory-mode VID/PID range
  that `aoahid_accessory_start` produces; the rules themselves are unchanged.

### Verification status

The new accessory start and Bulk Channels are tested only against the
deterministic fake libusb backend (including ThreadSanitizer runs and an
allocation probe of Channel read/write). None of it is hardware-verified: the
ten accessory-mode and Channel hypotheses in `docs/TARGET_MATRIX.md`,
including re-enumeration, HID in accessory mode, ADB next to HID on one handle
on Linux and Windows, and the first-report STALL behavior without library
retry, are recorded as **[unverified on hardware]**. No profile's status in
`TARGET_MATRIX.md` changes: every profile remains not hardware-verified.

## [1.0.0] - 2026-09-22

### Added

- `examples/c/verify/verify_toggle.c` and `verify_battery.c`, joining
  `verify_keyboard`/`verify_touch`/`verify_mouse`/`verify_all` as
  human-observable real-device verification programs (see
  `docs/QUICKSTART.md`). `verify_toggle` fires four Consumer Control Usages
  in turn (Play/Pause, Volume Increment, Mute, "AC New") for a media app to
  react to, deliberately excluding System Control Usages that could suspend
  or power off the phone mid-test. `verify_battery` sends a low/mid/high
  Battery Strength sequence and the explicit unknown (Null) state; unlike
  every other program here it produces no `getevent` output by design, so
  confirming it means checking `adb shell dumpsys battery` and
  `/sys/class/power_supply` instead.

### Fixed

- `aoahid_toggle(node, usage, 0)` (release) silently ignored `usage`
  entirely, always releasing whichever Usage was currently pressed
  regardless of what was passed. A caller that lost track of which Usage it
  had pressed could release the wrong one -- or release nothing, if nothing
  was pressed -- with no indication anything was wrong. `usage` on release
  must now be either `0` (release whichever Usage is pressed, for a caller
  that never tracked it -- the prior behavior, kept as an explicit opt-in)
  or the Usage actually pressed; a different nonzero Usage is rejected with
  `AOAHID_ERR_PARAM` instead of silently doing the wrong thing.
- `examples/c/verify/verify_toggle.c` did not set
  `aoahid_toggle_options.expected_linux_event_types`/`expected_linux_codes`,
  two of the four required parallel arrays (see `examples/c/profiles/toggle.c`
  for the reference shape), so `aoahid_spec_create_toggle` always failed with
  `AOAHID_ERR_UNSET_FIELD` and the program could never run.

### Verification status

`verify_toggle` and `verify_battery` were each run once against one physical
Samsung Android target after the fixes above, confirming both now run to
completion without error; `verify_toggle`'s Play/Pause, Volume Increment, and
Mute were also each observed changing that target's foreground media app.
This falls well short of `TARGET_MATRIX.md`'s "Minimum per-profile cases" for
either profile (every allowed Usage under foreground/background/screen-off,
zero re-arm, and -- for Battery -- the declared minimum/midpoint/maximum plus
`power_supply`/application-API association), so this release changes no
profile's status in `TARGET_MATRIX.md`: Toggle and Battery Strength remain
**not hardware-verified**, same as every other profile. The `usage`-on-release
fix and both new example programs touch neither the wire format nor the
generated descriptor of any profile.

## [0.5.3] - 2026-09-20

### Fixed

- Four generated profiles (Gamepad, Mouse, Pen, Touchscreen/Touchpad) could
  reject an otherwise-valid caller configuration with
  `AOAHID_ERR_PARAM`/"HID 1.11 section 8.4 forbids one field from spanning
  more than four bytes" even though every individual field width was within
  its own documented 1-32 bit maximum. The rejection depended on the bit
  offset a field happened to land at, which depended in turn on whether
  earlier, unrelated fields in the same report left a nonzero bit offset
  behind:
  - Gamepad: any axis width of 25-32 bits, after the Hat Switch or the raw
    D-pad OOC group (both 4 bits), or after a same-width axis before it in a
    multi-axis configuration (axis count 2 or more).
  - Mouse: a Wheel or AC Pan width of 25-32 bits after a non-byte-aligned
    X/Y or Wheel width.
  - Pen: a Pressure, Tilt X/Y, or Twist width of 25-32 bits after a
    non-byte-aligned X/Y or Pressure width.
  - Touchscreen/Touchpad: a Pressure, Width, Height, Azimuth, Scan Time, or
    Contact Count width of 25-32 bits after a non-byte-aligned Contact
    Identifier, X, or Y width.

  Each generator now inserts an explicit byte-boundary pad between these
  chained, independently caller-declared fields, so a field's own span
  calculation no longer depends on what a caller chose for the fields
  before it. See `LIMITS.md`, "Field ordering inside generated profiles",
  which documents this for the first time.

### Verification status

This release changes only the internal bit/byte layout of the four affected
profiles' generated descriptors where a caller's field widths previously hit
the byte-span rejection above or left unnecessary mid-report padding; it
adds no new public API and removes none. The publicly shipped example/golden
descriptors for Gamepad and Pen keep the exact same descriptor byte length
(the padding relocates rather than growing); Touchscreen and Touchpad grow
by a few descriptor bytes and a few report-data bits per contact, because
their golden fixtures exercise field widths that previously left mid-report
padding gaps this release now closes. No profile gains or loses a
hardware-verification claim; every profile remains **not hardware-verified**
(see `TARGET_MATRIX.md`).

## [0.5.2] - 2026-09-17

### Reverted

- All four runtime behavior changes from 0.5.1 (the `store_bits()`
  byte-aligned fast path, the `StateGuard` CPU-pause/yield spin backoff, the
  `LIBUSB_TRANSFER_ERROR` first-report retry, and the Hub/Mass
  Storage/Printer discovery filter) were reverted. None of them were
  measured against a benchmark or a real device before landing; they were
  implemented from a plan document's claims about where the bottlenecks
  were, not from profiling this codebase. The `store_bits()` and
  `StateGuard` changes in particular added real code complexity (four extra
  branch cases, a two-stage backoff) without evidence they were worth it.
  0.5.1 remains published as-is; this release reverts to the prior runtime
  behavior on top of it.

### Added

- `tools/release/sync_version.py`: a single command
  (`python tools/release/sync_version.py X.Y.Z`) that writes a new release
  version into every file that must carry it (`CMakeLists.txt`,
  `include/aoahid.h`, `vcpkg.json`, `tools/docs/Doxyfile`, the three binding
  manifests and their embedded version constants, and the ABI golden
  oracle's expected constants), instead of hand-editing eleven places.
  `tools/release/version_files.py` holds the single list of where the
  version lives; `validate_release.py` now reads that same list rather than
  duplicating it.
- Renamed `tests/abi/check_v0_5_1_golden.py` to `check_abi_golden.py`. Only
  its three `GOLDEN_CONSTANTS` version entries change across releases now,
  and `sync_version.py` updates those in place, so the file no longer needs
  a rename on every release.

### Verification status

This release only reverts 0.5.1's runtime changes and adds release
tooling; it changes no on-wire descriptor shape and no public ABI. It
neither adds nor retracts any hardware-verification claim. Every profile
remains **not hardware-verified** (see `TARGET_MATRIX.md`).

## [0.5.1] - 2026-09-17

### Changed

- `store_bits()` (the wire-report serializer used by every profile) now
  writes byte-aligned 8/16/24/32-bit fields directly instead of iterating
  bit-by-bit, which is the primary CPU cost for high-frequency multi-touch
  reports. The existing bit-by-bit path is unchanged and still handles every
  non-byte-aligned or irregular-width field.
- The internal-thread `StateGuard` spinlock now backs off with an
  architecture-specific pause instruction before falling back to
  `std::this_thread::yield`, instead of spinning a pure busy-wait. This
  avoids starving sibling hardware threads and reduces power draw under lock
  contention without adding syscall latency to the hot path.
- The first-report registration-race retry (for `f_accessory`'s delayed HID
  registration) now also triggers on `LIBUSB_TRANSFER_ERROR`, not only
  `LIBUSB_TRANSFER_STALL`. WinUSB commonly surfaces that same race as a
  generic pipe error rather than a stall.
- `aoahid_discover()` now skips USB Hub, Mass Storage, and Printer class
  devices before opening them, since AOA accessories never expose those
  classes on their primary descriptor. This avoids probing devices that can
  require elevation or trigger stricter host-controller behavior on Windows.

### Verification status

This release changes no on-wire descriptor shape, no public ABI, and no
Node-visible behavior; it is an internal performance and retry-robustness
release. It neither adds nor retracts any hardware-verification claim. Every
profile remains **not hardware-verified** (see `TARGET_MATRIX.md`), the same
status held since each profile was introduced.

## [0.5.0] - 2026-09-16

### Changed

- **Breaking:** Renamed `aoahid_touch_options` to `aoahid_touchscreen_options`
  for naming consistency with `aoahid_touchpad_options` and
  `aoahid_spec_create_touchscreen`. This is a pure rename; the struct layout,
  field list, and every other Touchscreen behavior are unchanged.
  `aoahid_touch_contact` and `aoahid_touch_extra` (used by `aoahid_touch()`,
  which serves both Touchscreen and Touchpad Nodes) are unaffected.

### Verification status

This release only renames a struct type; it changes no on-wire descriptor
shape and no runtime behavior, so it neither adds nor retracts any
hardware-verification claim. Touchscreen remains **not hardware-verified**
(a portable candidate; see `TARGET_MATRIX.md`), the same status it has had
since it was introduced.

## [0.4.0] - 2026-09-15

### Added

- **Touchpad profile.** `AOAHID_PROFILE_TOUCHPAD`, `aoahid_touchpad_options`,
  `aoahid_spec_create_touchpad`, and `aoahid_touchpad_button` add back a
  Touchpad Application Collection (Digitizers Touch Pad `0x0d/0x05`) as its
  own independent profile — not, as before its 0.3.0 removal, a
  `touchpad_button_count` field folded into the Touchscreen options.
  `aoahid_touch_options` / `aoahid_spec_create_touchscreen` are unchanged.

  `aoahid_touchpad_options` mirrors `aoahid_touch_options` field-for-field
  (same fixed-slot Multi-Touch contact shape) plus a `button_count` field for
  the Touchpad's physical click buttons; `button_count` may be zero for a
  buttonless clickpad. `aoahid_touchpad_button(node, button, pressed)` is a
  one-based, edge-guarded button state machine restored from the pre-0.3.0
  implementation, now gated to Touchpad Nodes only.

  Internally, Touchscreen and Touchpad now share one `TouchFields`
  representation, one descriptor/validation function, and one contact+button
  state machine; only the Application Collection Usage and the runtime
  `aoahid_capability_manifest.android_status` (Touchscreen stays a portable
  candidate; Touchpad is always conditional — Android converts Touchpad
  contacts to ordinary mouse-source motion, and gesture value-add is
  release/OEM dependent) differ between the two.

  A new C++ `touchpad_node_ref` wrapper offers `touch()` and `button()`;
  `touchscreen_node_ref` is unchanged (`touch()` only).

  This is a purely additive ABI change: every 0.3.0 struct, enum value, and
  function signature is unchanged.

  As with every profile in this repository, the Touchpad profile is
  **not hardware-verified**: it has not completed the four-level
  physical-device gate (`getevent`, `dumpsys input`, application API, and
  kernel device). Its `AOAHID_ANDROID_CONDITIONAL` status and the
  mouse-source-motion rationale above are `[Specified Linux/AOSP
  implementation observation]` and `[Android platform documentation]`, not a
  claim that this library's generated Touchpad descriptor has been
  exercised on a physical Android target. See `TARGET_MATRIX.md`.

## [0.3.0] - 2026-09-15

### Removed

- **Breaking:** Removed the Joystick Application Collection from the Gamepad
  profile. `aoahid_gamepad_options` no longer has an `application` field; the
  `aoahid_controller_application` type and its `AOAHID_CONTROLLER_GAMEPAD`/
  `AOAHID_CONTROLLER_JOYSTICK` enum were removed from the C ABI and every
  language binding. `aoahid_spec_create_gamepad` now always emits the Generic
  Desktop / Game Pad (`0x01/0x05`) Application Collection.

  This was not a capability regression. A source audit of the exact mainline
  Linux `hid-input.c` button-mapping code, the AOSP `EventHub.cpp`
  classification logic, and the shipped `Generic.kl` key layout showed that
  Gamepad already reaches everything Joystick could reach on Android:

  - Button-count capacity is identical for both Application Collections
    (`hid-input.c` maps buttons `0..15` to a dedicated 16-code range for
    either form, and buttons beyond that fall through to the same shared
    `BTN_TRIGGER_HAPPY` extension range for both).
  - `Generic.kl` maps the Gamepad button range (`BTN_GAMEPAD`) to the named
    Android game-controller keycodes (`BUTTON_A`, `BUTTON_B`, `BUTTON_X`,
    `BUTTON_Y`, `BUTTON_START`, `BUTTON_SELECT`, `BUTTON_THUMBL/R`, and so
    on), which is what `EventHub`'s `InputDeviceClass::GAMEPAD` check and
    `SOURCE_GAMEPAD` classification key on. The Joystick button range
    (`BTN_JOYSTICK`) maps only to the generic, unnamed `BUTTON_1..16`
    keycodes, so a Joystick-application descriptor could reach
    `SOURCE_JOYSTICK` but not the semantic `SOURCE_GAMEPAD` path that
    ordinary Android game-controller APIs expect.

  Keeping the Joystick option therefore cost real public-API surface and ABI
  stability for an alternative that Android classifies as a strict subset of
  Gamepad's outcome. A caller that specifically wants unnamed numbered
  buttons instead of semantic ones (for example, a flight-stick/HOTAS-shaped
  accessory) is expected to declare its Button Usage range directly; that
  need does not require a separate top-level Application Collection choice.

- **Breaking:** Removed the Touchpad Application Collection from the
  Touchscreen profile. `aoahid_touch_options` no longer has a
  `touchpad_button_count` field, and the `aoahid_touchpad_button` runtime
  function was removed from the C ABI, the C++ wrapper, and every language
  binding. `aoahid_spec_create_touchscreen` now always emits the Digitizers /
  Touch Screen (`0x0d/0x04`) Application Collection.

  Unlike Joystick, a Touchpad Application Collection is a real, distinct
  Linux input property (`INPUT_PROP_POINTER` versus a touchscreen's
  `INPUT_PROP_DIRECT`) and genuinely different Android products (external
  trackpad accessories for tablets are a real, common product category).
  It was removed for a narrower reason specific to this library's Input-only
  AOA transport: this guide's own Touchpad section already documented that
  Android converts touchpad contacts into ordinary mouse-source `MotionEvent`
  cursor movement rather than exposing raw contacts, and that the touchpad's
  own value-add over a plain mouse -- tap/click/gesture recognition -- "varies
  by Android release and OEM" and is explicitly `[Unverified on hardware]`.
  The already-existing, simpler, and portable-candidate Mouse profile reaches
  the one reliable outcome (cursor motion, buttons, and scrolling) that a
  Touchpad Application Collection was guaranteed to produce here, while the
  Touchpad wire format costs substantially more complexity (absolute
  fixed-slot multi-touch contact tracking versus plain relative deltas) for a
  gesture capability this project's own documentation could not promise.

  The fixed-slot Multi-Touch state machine itself, Contact ID/Count
  handling, and every other Touchscreen behavior are unchanged.

### Changed

- Simplified the Gamepad Android-status classification and D-pad-manifest
  conditions that previously included a redundant `dpad_representation ==
  AOAHID_DPAD_BUTTONS` clause; that clause was already implied by the
  adjacent `dpad_representation != AOAHID_DPAD_HAT` check and did not change
  behavior for any of the three `aoahid_dpad_representation` values.

### Verification status

This release removes two struct fields, one enum type and its two
constants, and one public function from the C ABI, and it changes the
Gamepad and Touchscreen profiles' on-wire descriptor shape whenever a caller
previously selected the Joystick or Touchpad form; it is a breaking-ABI
change within the pre-1.0 line. Neither removed form ever completed the
four-level physical-device gate (`getevent`, `dumpsys input`, application
API, and kernel device), so this release does not retract a hardware
verification claim -- both forms were already **not hardware-verified**, and
no profile in this release has completed that gate. The Android
kernel/`EventHub`/`Generic.kl` source citations above establish which
Application Collection Android's generic input stack favors; they are
source observations, not a claim that this
library's own generated descriptors have been exercised through that code
path on a physical device. See `TARGET_MATRIX.md`.

## [0.2.0] - 2026-09-14

### Changed

- **Breaking:** Consolidated the public profile surface from fourteen
  `aoahid_profile_kind` values to eight (`Keyboard`, `Mouse`, `Toggle`,
  `Gamepad`, `Touchscreen`, `Pen`, `Battery`, `Raw`). Every removed state
  machine is preserved byte-for-byte under a merged factory:
  - `aoahid_spec_create_barcode_wedge` merged into `aoahid_spec_create_keyboard`.
  - `aoahid_spec_create_consumer`, `_system_control`, `_camera_keys`, and
    `_telephony_keys` merged into `aoahid_spec_create_toggle`, which takes a
    new `aoahid_toggle_options` with explicit `application_page`,
    `application_usage`, and `field_page` fields so the caller states which
    HUT page it wants instead of the library inferring it from which function
    was called. The Camera Control (`field_page == 0x90`) Auto-focus/Shutter
    restriction is preserved as a validator rule keyed on that page.
  - `aoahid_spec_create_joystick` merged into `aoahid_spec_create_gamepad`;
    select it with `aoahid_gamepad_options.application =
    AOAHID_CONTROLLER_JOYSTICK`.
  - `aoahid_spec_create_touchpad` merged into `aoahid_spec_create_touchscreen`;
    select it by setting `touchpad_button_count` above zero.
  - Removed the Touchscreen one-contact-MT and pure-single-touch descriptor
    forms (`aoahid_touch_protocol`, and the `protocol`/`target_verified`
    fields of `aoahid_touch_options`). Every Touchscreen/Touchpad Spec is now
    the fixed-slot Multi-Touch form only.
- **Breaking:** Consolidated the runtime (per-report) API to one call per
  profile family, taking an explicit `down`/`0`-or-`1` argument instead of
  separate press/release-shaped functions:
  - `aoahid_keyboard_key_down`/`key_up`/`release_all` -> `aoahid_kbd(node,
    usage, down)`. There is no release-all call; release every Usage the
    caller pressed.
  - `aoahid_consumer_press`/`release`/`tap` and the redundant
    `aoahid_system_press`/`release`/`tap` (which only ever forwarded to the
    Consumer functions) -> `aoahid_toggle(node, usage, down)`.
  - `aoahid_gamepad_hat`/`aoahid_gamepad_dpad` -> `aoahid_dpad(node, up, down,
    right, left)`, which derives the canonical Hat encoding or the four raw
    D-pad bits depending on the Spec.
  - `aoahid_touch_down`/`_move`/`_up` -> `aoahid_touch(node, contact_id, down,
    x, y, extra)`, which auto-detects placement (a not-yet-active
    `contact_id`) versus movement (an already-active one) from `down=1`.
    Optional pressure/width/height/azimuth moved from `aoahid_touch_contact`
    to a new, separately passed `aoahid_touch_extra` (pass `NULL` to omit).
- The C++ header (`aoahid.hpp`) node-reference and factory-binding types were
  renamed/merged to match: `consumer_node_ref` -> `toggle_node_ref`;
  `barcode_wedge_node_ref`, `camera_keys_node_ref`, `telephony_keys_node_ref`,
  `system_control_node_ref`, `joystick_node_ref`, and `touchpad_node_ref` were
  removed in favor of their merged counterparts above.
- The Python, Rust, and C# bindings were updated to match this ABI; no
  language binding retains the removed symbols.
- **Breaking:** Removed the legacy 6KRO Array form from the Keyboard profile.
  `aoahid_keyboard_options` no longer has `rollover`, `array_length`,
  `usage_bit_width`, `bitmap_bits`, or
  `acknowledges_hid11_keyboard_array_conflict`; only `usage_minimum` and
  `usage_maximum` remain. The Keyboard profile is now exclusively a full
  N-Key Rollover (NKRO) one-bit Variable bitmap: every declared nonmodifier
  Usage gets its own bit, so any number of simultaneously pressed keys up to
  the declared range is reported at once, with no Array slot count and no
  `ErrorRollOver (0x01)` overflow encoding to inject or recover from. The
  `AOAHID_KEYBOARD_ARRAY`/`AOAHID_KEYBOARD_BITMAP` enum and the
  `aoahid_keyboard_rollover` type were removed from the C ABI and every
  language binding.

### Fixed

- **License compliance:** stopped publishing a bare `.so`/`.dll` as a
  standalone GitHub Release asset. Those files link libusb dynamically, but
  libusb's LGPL-2.1 text, copyright notice, and corresponding source lived only
  in the neighboring complete archive, so a loose file redistributed on its own
  carried a libusb-dependent binary with no license material in its own
  distribution unit, and the packaging self-check that guards the complete
  archives did not cover it. Each of the four targets now publishes a
  `*-runtime.tar.gz`/`*-runtime.zip` bundle containing the libaoahid shared
  library, the libusb runtime it loads, `LICENSE`, `NOTICE`,
  `THIRD_PARTY_NOTICES.md`, `build-metadata.json`,
  `third-party/libusb-copyright`, `third-party/source/libusb-1.0.30.tar.bz2`,
  the pinned vcpkg material on Windows, and a `README.txt` stating the
  MIT/LGPL split. Members are copied verbatim from the already validated shared
  archive, so they inherit its architecture and glibc validation, and the
  bundle is byte-for-byte reproducible. `collect_release.validate_runtime_bundle`
  fails the release if any license file is missing, if the libusb runtime is
  absent, if a bundled byte differs from the complete archive, or if an
  unexpected member appears. The release asset total is unchanged at 23.
 
- **License metadata:** raised the SPDX documents from package-level
  declarations with `NOASSERTION` on every file to per-file licensing.
  `generate_spdx.file_license` classifies each packaged file by installed
  location: `MIT` for first-party output, `LGPL-2.1-or-later` for everything
  installed from libusb, and MIT with Microsoft's copyright for the pinned
  vcpkg material. The libaoahid package record concludes
  `MIT AND LGPL-2.1-or-later` for the archive while still declaring `MIT` for
  libaoahid itself. Both `generate_spdx.validate_document` and
  `collect_release.validate_sidecar` reject a document whose file entries or
  package summary disagree.
- **License provenance:** added an `SPDX-License-Identifier: MIT` tag and
  copyright line to every first-party source file, so a file copied out of this
  repository stays identifiable without the root `LICENSE`. The new
  `tools/check_license_headers.py` enforces this in CI and can repair the tree
  with `--fix`.

- Fixed CI failures surfaced after the profile-consolidation work above:
  applied the project's `clang-format` style throughout, replaced the
  POSIX-only `nanosleep` call in the `examples/c/verify/` programs with a
  Windows/POSIX-portable `verify_sleep_ms` helper, silenced three
  `bugprone-easily-swappable-parameters` clang-tidy findings on
  `aoahid_kbd`/`aoahid_toggle`/`aoahid_touch`'s fixed public-ABI parameter
  order, and corrected the C# binding's `aoahid_touch` P/Invoke declaration
  to the `in TouchExtra` signature `tools/check_bindings.py` expects.
- Fixed `udev/51-aoahid.rules` and its `docs/PORTING.md` description, which
  only granted access for the unused Mode-B Google-Accessory VID/PID range;
  they now grant access through udev's `uaccess` tag, which is what Mode A
  (this library's only implemented mode) actually needs.

### Added

- Reduced fixed work on the report-send path. First-report STALL retries are
  the transport's only deferred work and arise solely from the registration
  race, so the Context now keeps an atomic count of armed retries. When it is
  zero - every ordinary send cycle - `poll` skips the device-registry lock, the
  per-slot scan, and the `steady_clock` read that computing a shortened wait
  would otherwise need. This is on the submit path and not only the poll loop,
  because caller-poll mode polls with a zero timeout immediately after
  accepting a report. The Context graveyard gained the same treatment: an
  atomic size is read without the Context mutex, so a poll with nothing
  awaiting reclamation takes no lock. Both counters are maintained across every
  arm, due-submit, cancel, and teardown transition, and the deterministic and
  ThreadSanitizer suites still pass. No latency figure is claimed; this is a
  structural reduction in instructions and lock acquisitions, not a
  measurement. See `docs/LATENCY.md`.
- Made each `examples/c/profiles/` program show how to actually send its
  profile. Every example already built a complete Spec and printed its
  descriptor, but the call sequence that makes a phone react lived only in the
  860-line `multi_profile.c`. Each example now also prints the full
  context-create/discover/open/node-open sequence together with its own
  profile-specific mutation calls, so a reader can see the whole path without
  leaving the file.
- Documented the Windows `adb`-holds-the-interface pitfall and added
  `docs/QUICKSTART.md`, a practical real-device setup and troubleshooting
  walkthrough distinct from the evidence-audit documents in `docs/`.
- Added `examples/c/verify/`: four small, heavily commented programs meant to
  be watched against a real phone rather than run unattended --
  `verify_keyboard`, `verify_touch`, and `verify_mouse` each exercise one
  profile, and `verify_all` alternates all three and then interleaves them
  fast enough to look simultaneous, within the single-threaded call
  serialization every `aoahid.h` function documents as required.
- Release archives (`tools/release/package_release.py`) now bundle
  `examples/` under `share/doc/libaoahid/examples/`; previously only the
  header, library, and license files were packaged.
- Disclosed in `README.md` that most of this repository's code, tests, and
  documentation were written by an AI coding assistant, with architecture,
  hardware verification, and review done by a human.

### Verification status

This release removes a public struct's fields and a public enum, and changes
the Keyboard profile's on-wire descriptor shape; it is a breaking-ABI change
within the pre-1.0 line. The bitmap/NKRO Keyboard form carries the same
`conditional` `android_status` it already had before this release; the
removed Array form was `portable candidate`, so a caller that depended on the
Array form's stronger classification, rather than only its ABI symbols, must
re-evaluate that dependency. No profile in this release has completed the
four-level physical-device gate (`getevent`, `dumpsys input`, application API,
and kernel device); Android device/profile behavior remains
**not hardware-verified** by these host-side CI jobs. See `TARGET_MATRIX.md`.

## [0.1.0] - 2026-08-27

### Changed

- Completed the generated-profile state-automation audit and made its boundary
  explicit. Keyboard and barcode-wedge Nodes route modifiers, pack key state,
  apply HID Array ErrorRollOver to every Array cell after the declared
  nonmodifier capacity is exceeded, and recover ordinary packing when the
  pressed set returns within that capacity; bitmap form keeps independent bits
  and has no Array rollover. Mouse Nodes maintain signed 64-bit accumulation
  and range-bounded fragmentation for X, Y, Wheel, and AC Pan.
- Consumer, System, Camera, and Telephony Nodes share one accepted-edge state
  machine for the six supported key-like descriptor semantics (Selector, OOC
  toggle, OOC maintained, MC, OSC, and RTC). The caller still declares the
  exact Usage semantic and target evidence; the library adds no timer or repeat
  behavior.
- Added boolean D-pad derivation for canonical Gamepad/Joystick Hat state:
  eight directions use wire values `0..7`, no direction uses deterministic
  Null `15`, adjacent pairs form diagonals, and opposite pairs are rejected
  without mutating state. Raw D-pad mode preserves four independent OOC bits;
  raw-D-pad Gamepads and every Joystick remain conditional and hardware
  unverified.
- Limited Gamepad portable-candidate manifests to the Android 17 CDD shape:
  Game Pad collection, canonical Hat, and a contiguous Button range beginning
  at `1` with at least five fields so A/B/X/Y Usages `1`, `2`, `4`, and `5`
  are present. No-D-pad Gamepads remain conditional.
- Ordered eraser-enabled pen Variable fields as Invert, Tip Switch, then In
  Range, matching the audited legacy descriptor-order dependency and the
  Android 17 ACK priority relation. Close-time neutralization also clears raw
  D-pad bits instead of repeating a held direction.
- Documented and regression-tested the remaining generated state machines:
  touch Count/continuation/Tip=0 Up/100-microsecond Scan Time handling; pen Away
  and tool-switch wire normalization; known-zero versus unknown-Null Battery
  Strength; shared keyboard behavior for barcode wedges; and caller-defined
  semantics for raw reports. Descriptor/product choices remain mandatory and
  no hardware-verification status was promoted.
- A zero value for `control_timeout_ms`, `send_timeout_ms`,
  `descriptor_fragment_bytes`, `transfer_pool_slots`,
  `maximum_report_bytes`, `close_drain_timeout_ms`,
  `first_report_attempts`, or `first_report_backoff_us` now selects the
  documented bounded project-policy fallback: 500 ms, 500 ms, 64 bytes, 8
  slots, 1024 bytes, 1000 ms, 20 total attempts, and 1000 microseconds,
  respectively. The timeout values are failure deadlines, not latency added to
  successful transfers, and are not described as libusb recommendations.
- A zero-initialized `aoahid_node_options` (`has_reserved_slots == 0` and
  `reserved_slots == 0`) now explicitly means no per-Node pool reservation.
  A positive reservation remains opt-in.
- Product- and target-specific choices, including coordinate/usage ranges,
  field widths, Report IDs, descriptor and EP0 policies, and exact-target HID
  parser policies, remain mandatory and receive no fallback.

### Removed

- Removed the Accessory Mode (Mode B) runtime startup path. The library now
  sends AOA HID requests only on the selected device's current EP0 and never
  sends identification strings, deprecated audio request 58, or
  `ACCESSORY_START`, and never waits for re-enumeration. Mode A remains
  target-conditional rather than a blanket Android compatibility claim.

### Compatibility

- Retained the numeric `AOAHID_START_ACCESSORY_MODE` token, `aoahid_aoa_strings`,
  and the former Mode-B members of `aoahid_device_options` solely as public ABI
  tombstones. Passing the retained mode returns `AOAHID_ERR_UNSUPPORTED` before
  USB I/O. Nonnull accessory strings or a nonzero deprecated-audio flag likewise
  return `AOAHID_ERR_UNSUPPORTED`; `reenumeration_timeout_ms` is ignored whether
  zero or nonzero. Their sizes and offsets are unchanged, so this removal does
  not silently reinterpret an existing C, C++, Python, C#, or Rust structure.
  This is a runtime capability removal, not a claim that Mode-B-only fields
  vanished from the stable binary layout.
- The tuning fallback change does not alter any public structure, field offset,
  or function signature and does not mutate a caller-owned options structure;
  the implementation normalizes a local copy. Explicit nonzero tuning values
  keep their prior meaning. The affected zero values previously failed
  validation, so no public unlimited-timeout behavior is being replaced.

### Fixed

- Made the installed static CMake target consistently
  `aoahid::aoahid_static` in static-only and combined packages. Static-only
  packages retain `aoahid::aoahid` as a compatibility/default alias, and the
  Linux and Windows release smoke tests now request the explicit static target.
- Made installed component availability require both the package's embedded
  build contract and its matching target export. Windows release jobs also
  use distinct shared/static build and staging trees and reject contaminated
  installed component exports before running positive and negative consumers.
  The release gate relies on the configure exit status and the installed
  exports instead of re-parsing generator-specific `CMakeCache.txt` text. A
  dedicated CTest injects a stale shared export into a static-only package and
  verifies that the embedded contract still rejects it. That metadata-only
  CTest does not start a nested native compiler, so it has the same behavior on
  Windows x64 and Windows ARM64 runners.
- Made strict documentation generation compatible with Ubuntu 22.04's
  Doxygen 1.9.1 by avoiding scoped empty-call spellings that it interprets as
  unresolved explicit links. A repository test protects every live Markdown
  input from reintroducing that parser-specific failure.
- Made the combined pkg-config CI checks derive their expected version from
  the canonical project metadata instead of duplicating a patch-version
  literal. A regression test rejects reintroducing such a fixed expectation.
- Made the installed CMake config establish `aoahid_FOUND` explicitly before
  validating requested components and derive available components from the
  installed target files. This keeps shared-only Windows packages consumable
  while preserving fail-closed rejection of unavailable components.
- Created the configured Doxygen output directory before invoking Doxygen in
  CI and Pages workflows, including Ubuntu 22.04's Doxygen 1.9.1 path.
- Restored strict Doxygen output to the workflow-validated
  `build/doxygen/html` path and kept documentation warnings fatal.
- Made the musl jobs install their required Python interpreter and avoided a
  GCC 14 `vector::insert(initializer_list)` false positive without disabling
  warnings. The equivalent test data is now appended after one bounded
  reservation.
- Made the libFuzzer executable explicitly require C++20, matching the
  production targets and headers it compiles.
- Replaced the stale fixed HID-descriptor count with an exact generated-versus-
  golden corpus count before parsing every descriptor with the pinned parser.
- Made actual-lib Windows builds define `NOMINMAX` before libusb includes, so
  the Windows SDK macros cannot corrupt `std::min` or
  the `max` member of `std::numeric_limits<T>` expressions. The release workflow also
  validates and passes the exact vcpkg libusb header and import library to
  CMake instead of relying on ambient package discovery.
- Made GNU ThreadSanitizer builds require GCC 12 or newer and pinned CI to GCC
  12 because GCC 11 has the unresolved condition-variable false positive in
  GCC bug 101978. No TSan warning class or project test is suppressed.
- Restricted the Mach-O shared-library export list to `_aoahid_*` and taught
  the export checker to exclude Darwin private-external and automatically
  hidden symbols reported by `nm -g`.
- Fixed the generated NuGet smoke program's missing success return, made
  binding version checks derive from the validated tag version, and hardened
  release publication against transient GitHub API failures before and after
  draft publication.
- Kept documentation generation mandatory while making GitHub Pages deployment
  opt-in through `AOAHID_DEPLOY_PAGES`, so a repository with Pages disabled no
  longer fails an otherwise valid documentation build.

These build, test, and publication corrections do not change the public C ABI
or promote any target from its hardware-unverified status.

### Added

- A stable C ABI and a thin C++ wrapper for explicit AOA HID construction,
  registration, state updates, and report submission.
- Keyboard and wedge; mouse; Consumer, System, camera, and telephony controls;
  gamepad/joystick; touch; pen; Battery Strength; and raw HID specification
  builders whose required choices are supplied by the caller.
- Caller-poll and internal-event-thread transport modes over libusb 1.0.
- Release workflow and package definitions for Ubuntu 22.04 glibc x86-64 and
  AArch64, and Windows x64 and ARM64, each in shared and static variants.
- SPDX 2.3 JSON SBOM, release-manifest, and SHA-256 checksum generation.
- A deterministic tagged-commit source archive covered by the release manifest
  and SHA-256 checksum set.
- Canonical package metadata for `https://github.com/nemarpuc/Libaoa_hid`.
- An Input-only runtime API. Generated descriptors may contain declarative
  Constant Feature metadata, but Feature transport remains unsupported.
- A Linux GNU/Clang `tsan` CMake preset and CI gate that instrument the full
  deterministic suite, including caller-poll and internal-thread paths.
- Deterministic checks for zero test-observed C++ allocations and no selected
  library mutexes in a prewarmed caller-poll keyboard send cycle, plus repeated
  completion-versus-close scheduling in internal-thread mode.
- A low-wakeup internal event pump with a bounded 60-second idle wait,
  retry-deadline shortening, explicit libusb interruption on teardown, and
  deterministic idle/cancellation/stop-wake tests.

### Verification status

The configured release gates exercise unit tests, a deterministic fake USB
transport, installed-package consumption, ABI export checks, and native
architecture checks. `TARGET_MATRIX.md` records whether an exact run and its
artifacts passed. Android device/profile behavior is **not hardware-verified**
by these host-side CI jobs. The evidence-qualified audit records and
target-specific limits govern these claims; this release makes no blanket
Android compatibility claim. ThreadSanitizer builds are race diagnostics, not
release-performance measurements, and a passing finite run is not a proof that
all possible schedules are data-race-free.
