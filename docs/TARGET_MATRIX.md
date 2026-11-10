# Target Matrix

This file records which combinations of host, Android device, and profile have
been confirmed on real hardware, and which have not. It states only what
[CHANGELOG.md](../CHANGELOG.md) records; a result that is not in a CHANGELOG
section is not listed here.

## Recorded results

From the CHANGELOG section for 0.1.0 (2026-11-07):

- 0.1.0 itself is **not hardware-verified** on a phone in its current form.
- Before the API reached that form, the transport and the device profiles
  Keyboard, Mouse, Toggle, Gamepad, Touchscreen, Touchpad, Pen and Digitizer
  were verified on a Samsung Galaxy Tab S11, from an Arch Linux host, through
  devtest and kernel `getevent`.
- The current library is tested against the fake libusb backend (including a
  ThreadSanitizer build), and the bindings against the compiled C ABI.

| Device | Host | Profiles | Library version |
|---|---|---|---|
| Samsung Galaxy Tab S11 | Arch Linux | Keyboard, Mouse, Toggle, Gamepad, Touchscreen, Touchpad, Pen, Digitizer, and the transport | Before the 0.1.0 API |

## Not recorded on hardware {#not-recorded-on-hardware}

- The Raw profile, and the battery report.
- Any host other than Arch Linux: Windows, macOS, Linux on AArch64.
- Any device other than the Samsung Galaxy Tab S11.
- Which Toggle pages and Gamepad forms the recorded run covered, and the Bulk
  Channel (ADB) and accessory-mode paths: the CHANGELOG names "the transport"
  without listing its parts.
- Any run of the 0.1.0 API.

Other Android versions, OEMs, and kernels have not been tested. AOA HID support
in the current USB mode depends on the device kernel; if a device STALLs the
first `aoahid_node_open`, try `aoahid_accessory_start` (see
[PROTOCOL.md](PROTOCOL.md#accessory-mode-vs-the-current-usb-mode)).

## Build targets

Release archives are built by `.github/workflows/release.yml`, one per target,
each holding both artifacts:

| Host | Runner | Artifacts |
|---|---|---|
| Linux x86_64, glibc | `ubuntu-22.04` | `libaoahid.so`, `libaoahid_static.a` |
| Linux aarch64, glibc | `ubuntu-22.04-arm` | `libaoahid.so`, `libaoahid_static.a` |
| Windows x86_64 | `windows-2025-vs2026` | `aoahid.dll` + import library, `aoahid_static.lib` |
| Windows ARM64 | `windows-11-vs2026-arm` | `aoahid.dll` + import library, `aoahid_static.lib` |
| macOS arm64 | `macos-15` | `libaoahid.dylib`, `libaoahid_static.a` |
| macOS x86_64 | `macos-15-intel` | `libaoahid.dylib`, `libaoahid_static.a` |

The macOS archives are built with the fake libusb backend's tests and an
installed-package consumer, and have not been tested with a real device or
libusb's macOS backend.

`.github/workflows/ci.yml` also builds and tests Linux musl (x86_64 and
aarch64). That is a portability check only; no release archive is published
for it, and it has not been tested with a real device.

## Checking a new device

To add a device or profile, record at least:

- device model, Android version, and build fingerprint (`adb shell getprop ro.build.fingerprint`);
- host OS, architecture, libusb version, and backend;
- the libaoahid version or commit;
- `adb shell getevent -lp` for the new input node and `adb shell getevent -lt`
  while sending input;
- the relevant `adb shell dumpsys input` block; and
- whether the input took effect in a foreground app.

`tools/device-check/` has a capture script and a small Android app for the
last three items. Keep one row per exact device/build/host combination; do not
merge results from different devices or Android versions.

Suggested cases per profile:

| Profile | Cases |
|---|---|
| Keyboard | Modifier chords; release all; more than six non-modifier keys held at once; key repeat; layout and IME differences |
| Mouse | Positive and negative X/Y; every button; wheel; AC Pan; high-rate motion; pointer capture |
| Toggle | Every allowed Usage; press and release; screen off; foreground and background media/camera/call handling |
| Gamepad | Every button and axis; neutral; minimum and maximum; all eight Hat directions and the null (centered) value, or each raw D-pad bit |
| Touchscreen | One to the configured maximum contacts; stable Contact IDs; explicit lift; edges and corners; optional pressure/azimuth fields; Scan Time when enabled |
| Touchpad | Touchscreen cases plus each button; observe the Android motion source |
| Pen, Digitizer | Hover; tip; eraser in and out; barrel buttons; pressure, tilt, and twist when enabled; four corners; pen with touch |
| Battery report | Kernel `CONFIG_HID_BATTERY_STRENGTH`; minimum, midpoint, maximum; the device's `power_supply` directory, class `BATTERY` in `dumpsys input`, and `InputDevice.getBatteryState()` from an app |
| Teardown | Neutral report; close; unplug with a report in flight; reopen; Android reboot |
