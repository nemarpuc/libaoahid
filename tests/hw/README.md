# Hardware checks

The unit and sim tests run against memory and a fake libusb. They cannot show
what only a phone shows, so a change that touches the transport, a profile's
descriptor or report bytes, or teardown is also checked by hand on a real
Android device. Nothing here runs in ctest or CI.

## What the sim layer cannot show

- **Real USB electrical behaviour**: cables, hubs, power, a phone unplugged
  mid-transfer, and the statuses a real host controller returns.
- **Enumeration on real Android builds**: whether a given Android version or
  OEM build accepts the AOA HID registration (requests 54 to 57) and which
  input class it gives the device.
- **libusb backend differences per OS**: usbfs on Linux, WinUSB or libusbK on
  Windows, IOKit on macOS; driver binding and interface claims differ.
- **Accessory-mode switch timing**: how long a phone takes to re-enumerate
  after request 53, and what it does with requests sent too early.
- **What the user sees**: that a foreground app receives the key, motion,
  touch or pen events, and that Android's own handling (IME, media keys,
  pointer capture) does what the profile intends.

## What to run

1. Build the library and the verification programs with the real libusb
   (`AOAHID_USE_FAKE_LIBUSB=OFF`, `AOAHID_BUILD_EXAMPLES=ON`), or as in
   [`examples/README.md`](../../examples/README.md).
2. Connect the phone and run the programs in `examples/c/verify/` for the
   profiles you changed: `verify_accessory` switches the phone to accessory
   mode, then `verify_keyboard`, `verify_mouse`, `verify_touch`,
   `verify_toggle`, or `verify_all`. Each prints what it sends; watch it take
   effect on the phone.
3. For a new device or a profile not covered by those programs, follow
   [`docs/TARGET_MATRIX.md`](../../docs/TARGET_MATRIX.md#checking-a-new-device):
   run `tools/device-check/capture.sh <adb-serial> /dev/input/eventN <seconds> <output-directory>`
   while sending input, and the input test app in `tools/device-check/app/`
   to see what a foreground app receives
   ([`tools/device-check/README.md`](../../tools/device-check/README.md)).
   Exercise the cases the matrix suggests for the profile.
4. For teardown changes, also: close with a report in flight, unplug with a
   report in flight, reopen, and reboot the phone.
5. For latency claims, instrument the boundaries `t0` to `t7` described in
   [`docs/LATENCY.md`](../../docs/LATENCY.md#measurement-model) on a release
   build. A number without its host, CPU, compiler, libusb backend, phone and
   percentiles is not recorded.

## What to record

For each exact device, Android build and host combination (never merged with
another):

- device model, Android version and `ro.build.fingerprint`;
- host OS, architecture, libusb version and backend (and the driver on
  Windows);
- the libaoahid version or commit;
- the `capture.sh` output, the test app log, and the verify programs' output;
- per profile and case, pass or fail, decided by you; the tools do not
  decide.

## Where results go

- **`CHANGELOG.md`**: the release entry says whether it was
  hardware-verified, and on what, in one line (as the `[0.1.0]` entry does).
- **`docs/TARGET_MATRIX.md`**: one row per combination under
  "Hardware-tested combinations", and the per-profile status.
- **`docs/LATENCY.md`**: measured figures, with everything they were measured
  on, in a measured table; nothing unmeasured.

Keep the raw captures with the change (an issue, the pull request, or a
release note) so the row can be traced back to them.
