# Quickstart Guide

This walkthrough takes you from a fresh checkout to a phone reacting to real
HID reports.

You need:

- an Android device that reports AOA version 2 (see
  [TARGET_MATRIX.md](TARGET_MATRIX.md) for what has been tested);
- a USB data cable from the computer to the device;
- CMake 3.20 or later, a C++20 compiler, and libusb 1.0.30 or later.

---

## 1. Install the build tools and libusb 1.0.30

libusb 1.0.30 or newer is required on every platform. Without it, CMake stops
with `libusb headers report LIBUSB_API_VERSION older than 0x0100010C (libusb
1.0.30).` when it finds an older libusb, or `libusb 1.0.30 or newer headers
and library were not found.` when it finds none (`CMakeLists.txt`). Check what
you have with `pkg-config --modversion libusb-1.0`.

Arch Linux packages a current libusb:

```sh
sudo pacman -S cmake gcc libusb
```

Debian and Ubuntu: most releases package an older libusb, so
`apt install libusb-1.0-0-dev` alone is not enough. Debian 12 has 1.0.26 and
Debian 13 has 1.0.28 ([sources.debian.org](https://sources.debian.org/src/libusb-1.0/));
Ubuntu 22.04 has 1.0.25 and Ubuntu 24.04 has 1.0.27
([Launchpad](https://launchpad.net/ubuntu/+source/libusb-1.0)); both retrieved
2026-11-10. Use one of these instead:

- **Build libusb 1.0.30 from its release archive** into a prefix of your own,
  and point CMake at it with `PKG_CONFIG_PATH`:

  ```sh
  sudo apt install cmake g++ make pkg-config libudev-dev
  curl -LO https://github.com/libusb/libusb/releases/download/v1.0.30/libusb-1.0.30.tar.bz2
  tar xf libusb-1.0.30.tar.bz2
  (cd libusb-1.0.30 && ./configure --prefix="$HOME/.local/libusb-1.0.30" && make && make install)
  export PKG_CONFIG_PATH="$HOME/.local/libusb-1.0.30/lib/pkgconfig"
  ```

  The programs built in step 4 find this libusb through their build-tree
  `RPATH`; anything else needs `LD_LIBRARY_PATH="$HOME/.local/libusb-1.0.30/lib"`.
- **Use vcpkg**: the repository's `vcpkg.json` asks for `libusb` 1.0.30 or
  later ([BUILDING.md](BUILDING.md#vcpkg)).
- **Skip the source build**: each release archive ships libusb 1.0.30 (header,
  `libusb-1.0.pc`, and the shared library) next to libaoahid
  ([RELEASES.md](RELEASES.md#binary-archive-layout)).

Windows: install CMake, a recent Visual Studio (or the Build Tools) with the
C++ workload, and libusb 1.0.30 or newer, for example through vcpkg.

## 2. Check the cable

A cable or hub port marked with a battery or lightning-bolt icon is often
**charge-only**: it has no data lines, and the phone never appears in USB
enumeration through it. From a hub, use a port labeled as a data port (for
example "USB 3.0"), not a "PD" or charging port meant for feeding power into the
hub. If in doubt, plug the phone straight into the computer for the first try.

Then confirm the host sees the phone at all: `lsusb` on Linux, or Device
Manager on Windows (the phone is usually under "Portable Devices"). If it is
not listed, this library cannot see it either.

## 3. Give your user access to the USB device

### Linux

Your distribution may already give the logged-in user access to the phone
(for example through `uaccess` rules shipped for MTP or ADB). If
`aoahid_device_open` fails with `AOAHID_ERR_ACCESS`, install the udev rule:

```sh
sudo cp udev/51-aoahid.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Then reconnect the phone.

The active rule in that file is broad: it tags **every** USB device (not
interfaces) with `uaccess`, so any program run by the user at the local seat
can open any USB device, not just the phone. For a narrower grant, delete that
line and use the per-vendor group rule commented in the same file
(`ATTR{idVendor}=="<your VID from lsusb>", MODE="0660", GROUP="plugdev"`); the
file's last rule already grants the same group the accessory-mode IDs
`18d1:2d00` to `18d1:2d05`. See also [SECURITY.md](../SECURITY.md#scope-and-threat-model).

For a quick test you can skip this and run the programs with `sudo`. If a
program fails with `AOAHID_ERR_ACCESS` but works with `sudo`, the problem is
USB permissions.

### Windows

libusb can use the phone only through WinUSB, libusbK, or libusb0
([PROTOCOL.md](PROTOCOL.md#host-side-caveats)). Two things commonly get in the
way:

- A running `adb` server holds the phone's ADB interface. Run
  `adb kill-server`, and close any app that might restart it (Android Studio,
  scrcpy, Vysor). To use `adb` on the same phone while your program runs, see
  [ADB on the same phone](../README.md#adb-on-the-same-phone).
- The manufacturer's own driver is bound instead of WinUSB, so libusb cannot
  claim the interface you need. Replacing the device's driver with WinUSB
  using [Zadig](https://zadig.akeo.ie/) may help; see the Windows section of
  [PORTING.md](PORTING.md#windows). If only HID is needed and the device opens,
  no driver change is required.

## 4. Build the library and check the link

`AOAHID_USE_FAKE_LIBUSB` selects the test-only libusb stand-in used by
`ctest`, which never touches real USB hardware. It defaults to `OFF`; the
`dev`, `tsan`, and `coverage` presets turn it on. Keep it `OFF` for a real
device:

```sh
cmake -S . -B build -DAOAHID_USE_FAKE_LIBUSB=OFF -DAOAHID_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cd build/examples
```

The programs are in `build/examples/`; with a multi-config generator such as
Visual Studio they are next to `aoahid.dll`, in `build/Release/`.

Run the multi-profile example (`sudo` only if you skipped step 3):

```sh
sudo ./aoahid_example_c
```

It prints every device that answers AOA request 51 (bus, address, port path,
VID:PID, AOA version), opens each one that reports version 2 or later,
registers its profiles, sends a few reports, and exits with status `0`. Every
state change it makes is brief,
because the same program also runs unattended in CI; it is not meant to be
watched.

## 5. Run the programs made to be watched

`examples/c/verify/` holds small programs meant to be watched. Each prints
what to do before it starts and pauses so you have time to react:

| Program | What to do first | What you should see |
|---|---|---|
| `aoahid_verify_keyboard` | Open a text field (Notes, a search box, anything with a text cursor) | The text `hello from libaoahid` typed three times |
| `aoahid_verify_mouse` | Nothing | The cursor moves in circles for a few seconds. Many phones show no cursor without a pointer or desktop mode enabled; a clean exit means the reports were accepted |
| `aoahid_verify_touch` | Have any screen open | One finger drags left and right across the screen five times |
| `aoahid_verify_toggle` | Unlock the phone and put a media app in the foreground (ideally a paused track) | Play/Pause, Volume Increment, Mute, and AC New fire in turn; watch the media app or `adb shell getevent -lt`. Only Consumer Control Usages are sent; the comment at the top of the file says why System Control is left out |
| `aoahid_verify_accessory <manufacturer> <model>` | Open a text field; the two strings are your product values (Android matches them against an app's accessory filter and may show a "no app" prompt) | The phone disconnects and comes back in accessory mode (`18d1:2d00`, or `2d01` with USB debugging), one `a` is typed, and with `2d01` an ADB Channel opens and closes on the same USB handle. Unplug and replug to leave accessory mode |
| `aoahid_verify_all` | Open a text field | Touch circles, `hello world` typed, and mouse circles, one profile at a time for two rounds, then all three interleaved |

```sh
sudo ./aoahid_verify_keyboard
sudo ./aoahid_verify_touch
sudo ./aoahid_verify_mouse
sudo ./aoahid_verify_toggle
sudo ./aoahid_verify_all
sudo ./aoahid_verify_accessory "Your Company" "Your Model"
```

Drop `sudo` once USB permissions are set up (step 3), and on Windows.

Each program prints the field and reason from `aoahid_last_error()` when a
call fails, and exits with a nonzero status.

## Troubleshooting

| Symptom | Likely cause | What to check |
|---|---|---|
| CMake stops at `libusb headers report LIBUSB_API_VERSION older than ...` | An older libusb was found | Step 1 |
| Phone never appears in `lsusb` / Device Manager | Charge-only cable or hub port | Step 2 |
| `AOAHID_ERR_ACCESS` on Linux | Missing udev permission | Step 3, or run with `sudo` to isolate the cause |
| Device open or Channel open fails only on Windows | `adb` holds the interface, or a manufacturer driver is bound instead of WinUSB | Step 3, Windows |
| Program exits `0`, nothing visible happens | No focused text field (keyboard), or you ran `aoahid_example_c`, whose changes are too brief to see | Step 5, and focus a text field first for the keyboard |
| A physical-keyboard indicator appears but no character | No text field was focused at that moment | Focus a text field and run again |
| `AOAHID_ERR_STALL` from the first `aoahid_node_open` | The device has no AOA 2.0 HID support (open does not check), or it accepts HID requests only in accessory mode | Check the `protocol_version` discovery reported; otherwise try `aoahid_accessory_start` ([PROTOCOL.md](PROTOCOL.md#accessory-mode-vs-the-current-usb-mode)) |

These programs are a quick check. What has been confirmed on real hardware is
in [TARGET_MATRIX.md](TARGET_MATRIX.md).
