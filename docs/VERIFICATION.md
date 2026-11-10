# Verification

This page documents what is verified in `libaoahid`, which verification layer
covers each feature, the exact hardware-verification status, line and branch
coverage numbers over `src/`, and known platform-specific open items.

For how to run and extend the test suites, see
[tests/README.md](../tests/README.md). For running the hardware checks on a
physical Android phone, see [tests/hw/README.md](../tests/hw/README.md),
[tools/device-check/README.md](../tools/device-check/README.md), and
[TARGET_MATRIX.md](TARGET_MATRIX.md).

## Verification layers

`libaoahid` separates verification into three functional layers plus static ABI
and packaging gates:

1. **`unit` (`tests/unit/`, CTest `aoahid.unit`).** Exercises pure in-memory
   components that do not touch libusb: HID 1.11 short-item encoding
   (`ItemWriter`), profile descriptor generation and layout (`DescriptorBuilder`),
   every option field of every profile (`spec_options`), raw descriptor parsing
   and report-table matching (`raw_descriptor`), and report state serialization.
2. **`sim` (`tests/sim/`, CTest `aoahid.sim*`).** Drives the full public C ABI
   and `src/transport/` against the deterministic in-memory fake libusb in
   `tests/fake_libusb/` (`-DAOAHID_USE_FAKE_LIBUSB=ON`). Tests AOA control
   requests 51–57, Bulk Channels, caller-poll and internal-thread event modes,
   context-domain serialization (`aoahid.sim-serialization-check-{ok,violation}`),
   and every heap-allocation failure point (`aoahid.sim-allocation-failure`).
3. **`hw` (`tests/hw/`, `examples/c/verify/`, `tools/device-check/`).** Manual
   runs against a physical Android phone over a USB cable. Only a real phone
   tests kernel `f_accessory` / `uhid` / `hid-input` parsing, Android
   `EventHub` / `InputReader` classification and `.kl` / `.idc` rules, host OS
   USB drivers (`usbfs`, `WinUSB`, `IOKit`), and physical bus timing.

## Feature verification matrix

| Feature / area | `unit` (`tests/unit/`) | `sim` (`tests/sim/`, fake libusb) | `hw` (real phone via `examples/c/verify/` & `tools/device-check/`) |
| --- | --- | --- | --- |
| HID 1.11 short-item encoding (`ItemWriter`) | `test_item_writer.cpp` (`item_writer`), `fuzz_descriptor.cpp` | Exercised through all profile and session tests | Exercised by every registered profile |
| Generated descriptor structure & golden bytes (`DescriptorBuilder`) | `test_profiles.cpp`, `tests/golden/*.hex` (`aoahid.golden-descriptors`), `hid-tools` CI gate | `test_session_setup.cpp` | Exercised for the 8 hardware-tested profiles below |
| Profile option validation (`aoahid_*_options_init`, `aoahid_spec_create_*`) | `test_spec_options.cpp` (`spec_options`), `symbol_lint.py` zeroed-struct gate | `test_api_arguments.cpp` (`api_arguments`) | Exercised by `examples/c/verify/*` and `aoahid_player` |
| Keyboard profile (NKRO bitmap + modifiers) | `test_profiles_keyboard.cpp` (`profiles`), `keyboard.hex` | `test_session_setup.cpp`, `test_submit.cpp`, `test_report_state.cpp`, `test_neutral_close.cpp` | Verified (`verify_keyboard`, `aoahid_player`) on Galaxy Tab S11 & POCO F6 Pro (Windows 10 x64 & Arch Linux, 2026-09) |
| Mouse profile (buttons, X/Y deltas, wheel, AC Pan) | `test_profiles_mouse.cpp` (`profiles`), `mouse-wheel-pan.hex` | `test_submit.cpp`, `test_report_state.cpp`, `test_neutral_close.cpp` | Verified (`verify_mouse`, `aoahid_player`) on Galaxy Tab S11 & POCO F6 Pro (Windows 10 x64 & Arch Linux, 2026-09) |
| Touchscreen profile (Fixed-slot multi-touch, Scan Time, optional geometry/pressure) | `test_profiles_touch.cpp` (`profiles`), `touchscreen-fixed-mt.hex` | `test_touch_battery.cpp`, `test_report_state.cpp`, `test_neutral_close.cpp` | Verified (`verify_touch`, `aoahid_player`) on Galaxy Tab S11 & POCO F6 Pro (Windows 10 x64 & Arch Linux, 2026-09) |
| Touchpad profile (`INPUT_PROP_POINTER`, contacts + external buttons) | `test_profiles_touch.cpp` (`profiles`), `touchpad.hex` | `test_touch_battery.cpp`, `test_report_state.cpp`, `test_neutral_close.cpp` | Verified on Galaxy Tab S11 (Arch Linux, 2026-10: two-finger scroll, pinch zoom, three-finger swipe, tap-to-click, button click); not tested on POCO F6 Pro or Windows |
| Pen profile (`INPUT_PROP_DIRECT`, hover, tip, barrel, eraser, invert, pressure, tilt) | `test_profiles_pen.cpp` (`profiles`), `pen.hex` | `test_report_state.cpp`, `test_neutral_close.cpp` | Verified on Galaxy Tab S11 (Arch Linux, 2026-10); not tested on POCO F6 Pro or Windows |
| Digitizer profile (`INPUT_PROP_POINTER`, indirect tablet) | `test_profiles_pen.cpp` (`profiles`), `digitizer.hex` | `test_report_state.cpp`, `test_neutral_close.cpp` | Verified on Galaxy Tab S11 (Arch Linux, 2026-10); not tested on POCO F6 Pro or Windows |
| Gamepad profile (Hat Switch, D-pad buttons, or no D-pad; buttons; up to 9 axes) | `test_profiles_gamepad.cpp` (`profiles`), `gamepad.hex` | `test_report_state.cpp`, `test_neutral_close.cpp` | Verified (`aoahid_player`, all three D-pad modes: `AOAHID_DPAD_HAT`, `AOAHID_DPAD_BUTTONS`, and `AOAHID_DPAD_NONE`) on Galaxy Tab S11 & POCO F6 Pro (Windows 10 x64 & Arch Linux, 2026-09) |
| Toggle profile: Consumer Control (`0x0C`) | `test_profiles_toggle.cpp` (`profiles`), `consumer-controls.hex` | `test_report_state.cpp`, `test_neutral_close.cpp` | Media keys verified (`verify_toggle`, `aoahid_player`) on Galaxy Tab S11 & POCO F6 Pro (Windows 10 x64 & Arch Linux, 2026-09) |
| Toggle profile: System Control (`0x01`), Camera (`0x90`), Telephony (`0x0B`) | `test_profiles_toggle.cpp` (`profiles`), `system-controls.hex`, `camera-keys.hex`, `telephony-keys.hex` | `test_report_state.cpp` | **Not verified on hardware** |
| Battery report (`enable_battery`, `aoahid_battery_update`) | `test_profiles_battery.cpp` (`profiles`), `keyboard-battery.hex`, `examples/c/profiles/keyboard_battery.c` | `test_touch_battery.cpp`, `test_report_state.cpp` | `BATTERY` device class registration via `dumpsys input` confirmed on Samsung Galaxy Tab S11 (Arch Linux, 2026-10-11; `examples/c/profiles/keyboard_battery.c`); live percentage reporting to Android UI not yet verified |
| Validated raw descriptor (`AOAHID_PROFILE_RAW`, `aoahid_raw_submit`) | `test_raw_validator.cpp` (`raw_descriptor`), `test_profiles_raw.cpp` (`profiles`), `raw-validated.hex`, `fuzz_descriptor.cpp` | `test_session_setup.cpp`, `test_submit.cpp`, `test_report_state.cpp` | **Not verified on hardware** |
| USB discovery (`aoahid_discover`, request 51 `ACCESSORY_GET_PROTOCOL`) | — | `test_identity.cpp` (`identity`), `test_transport_failures.cpp` (`transport_failures`) | Verified on Galaxy Tab S11 & POCO F6 Pro (reported AOA version 2) |
| Accessory mode switch (`aoahid_accessory_start`, requests 51–53) | — | `test_accessory_start.cpp` (`transport`), `test_transport_failures.cpp` (`transport_failures`) | Verified (`verify_accessory`) on Galaxy Tab S11 & POCO F6 Pro (switched to `18D1:2D00`/`18D1:2D01` and accepted HID input) |
| Device open, Linux HID policy check & shared `Port` (`aoahid_device_open`) | — | `test_device_open.cpp`, `test_session_setup.cpp`, `test_transport_failures.cpp` | Verified on Galaxy Tab S11 & POCO F6 Pro from Windows 10 x64 and Arch Linux |
| Node registration, descriptor slicing & close (`aoahid_node_open`, `aoahid_node_close`, requests 54, 56, 55) | — | `test_session_setup.cpp`, `test_node_close.cpp`, `test_neutral_close.cpp`, `test_transport_failures.cpp` | Verified on Galaxy Tab S11 & POCO F6 Pro (including 6–16 ms post-open STALL window and ~60 ms input-reader attach window on Galaxy Tab S11) |
| Report submission, transfer pool & reservations (`aoahid_node_submit`, `aoahid_node_submit_blocking`, request 57) | — | `test_submit.cpp`, `test_hot_path.cpp`, `test_internal_thread.cpp`, `test_transport_failures.cpp` | Verified for the 8 hardware-tested generated profiles |
| Bulk Channels (`aoahid_channel_open`, `read`, `write`, `close`; `STREAM` & `REQUEST` modes; ZLP) | — | `test_channel.cpp`, `test_internal_thread.cpp`, `test_transport_failures.cpp` | Verified in current USB mode on ADB bulk interface (`FF/42/01`) via `aoahid_adb_proxy` alongside HID input on Samsung Galaxy Tab S11 (Windows 10 x64, 2026-09; Arch Linux, 2026-10); simultaneous ADB Bulk Channel + HID Node in `18D1:2D01` accessory mode not tested on hardware |
| Event modes, teardown & Context graveyard (`AOAHID_EVENT_CALLER_POLL`, `AOAHID_EVENT_INTERNAL_THREAD`, `aoahid_device_close`, `aoahid_context_destroy*`) | — | `test_transport_runtime.cpp`, `test_context_destroy.cpp`, `test_internal_thread.cpp` (`aoahid.sim-internal-thread`) | Exercised by `examples/c/verify/*` and `aoahid_player` |
| Context serialization check (`AOAHID_CHECK_SERIALIZATION`) | — | `test_serialization_check.cpp` (`aoahid.sim-serialization-check-ok`, `aoahid.sim-serialization-check-violation`) | Host-only runtime check |
| Heap allocation failure resilience (`std::nothrow` & C ABI catch firewall) | — | `test_allocation_failure.cpp` (`aoahid.sim-allocation-failure`), `test_hot_path.cpp` | Host-only runtime check |
| Public C ABI, C++ header & bindings (Python, C#, Rust) | `test_c_abi.c` (`aoahid.c-abi`), `test_cpp_wrapper.cpp` (`aoahid.cpp-wrapper`), `check_abi_golden.py` (`aoahid.abi-layout-oracle`), `symbol_lint.py`, `bindings/*/tests` | `examples/python/enumerate.py`, `examples/csharp/Enumerate.csproj`, `bindings/rust/aoahid-sys/examples/enumerate.rs` | C and C++ used on hardware; Python, C#, and Rust verified against compiled C ABI layout oracle and fake libusb |
| CMake package, `pkg-config` & release archives | `tests/cmake/*`, `tests/release/test_release_tools.py` | `tools/release/consumer` (`shared`, `static`, `shared;static`) | Host packaging gate |

## Hardware verification status

- **Release `0.1.0` itself is not hardware-verified.** The public C ABI around
  the transport and profiles was reworked since the September and October 2026
  hardware runs (timeouts moved to call arguments, `*_init` functions added,
  Linux HID policy grouped into `aoahid_linux_hid_policy`; see
  [CHANGELOG.md](../CHANGELOG.md)), and the current tree has not been re-run on
  a phone yet.
- **What was verified on hardware in 2026-09 and 2026-10** (see
  [TARGET_MATRIX.md](TARGET_MATRIX.md) for per-device details):
  - **Devices:** Samsung Galaxy Tab S11 (Android 16) and Xiaomi POCO F6 Pro
    (Android 15, HyperOS 2).
  - **Hosts:** Windows 10 x64 and Arch Linux x86-64.
  - **Profiles:** Touchscreen, Keyboard, Mouse, Gamepad (Hat Switch, raw D-pad
    buttons, and no D-pad), and Toggle (Consumer Control media keys) took
    effect on both devices from both hosts (2026-09). Touchpad (two-finger
    scroll, pinch zoom, three-finger swipe, tap-to-click, button click), Pen,
    and Digitizer took effect on the Samsung Galaxy Tab S11 under Arch Linux
    (2026-10). For a Keyboard with a battery report, `BATTERY` device class
    registration via `dumpsys input` was confirmed on the Samsung Galaxy Tab S11
    under Arch Linux (2026-10-11).
  - **Transport & Channels:** Discovery (`aoahid_discover`, reporting AOA
    version 2), accessory-mode switching (`verify_accessory`, re-enumerating as
    `18D1:2D00`/`18D1:2D01` and accepting HID input), and HID registration
    without switching USB mode first worked on both devices from both hosts.
    An ADB Bulk Channel (`FF/42/01`, via `aoahid_adb_proxy`) alongside an AOA
    HID Node on one handle was verified in the current USB mode on the Samsung
    Galaxy Tab S11 (Windows 10 x64 after whole-device WinUSB driver replacement,
    2026-09; Arch Linux `devtest` + `getevent`, 2026-10).
- **What has not been tested on hardware:**
  - Validated raw descriptors (`AOAHID_PROFILE_RAW` / `aoahid_raw_submit`).
  - Toggle profiles for System Control (`0x01`), Camera Control (`0x90`), and
    Telephony (`0x0B`).
  - Live Battery Strength percentage reporting to the Android UI
    (`enable_battery` / `aoahid_battery_update`; `BATTERY` device class
    registration in `dumpsys input` was confirmed on the Samsung Galaxy Tab S11
    on 2026-10-11, while reading back the reported percentage was not verified).
  - Simultaneous ADB Bulk Channel and AOA HID Node in `18D1:2D01` accessory mode.
  - Touchpad, Pen, and Digitizer on the POCO F6 Pro or from a Windows host.
  - macOS hosts (built and tested against the fake libusb in CI; never tested
    with a physical Android device).
  - Linux AArch64 and Windows ARM64 with a physical device.

## Coverage

The `Coverage` CI job builds the library and test suites with
`-DAOAHID_COVERAGE=ON` (`--coverage -O0 -fprofile-update=atomic`), runs CTest
against the fake libusb, and checks `src/` with `gcovr`. The CI gate enforces a
floor of **87% of lines** and **75% of branches** across `src/`
(`--exclude-throw-branches --exclude-unreachable-branches`).

To reproduce the coverage report locally (requires GCC or Clang and `gcovr`):

```sh
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage --output-on-failure
mkdir -p build/coverage/coverage
gcovr --root . --object-directory build/coverage --filter 'src/' \
  --exclude-throw-branches --exclude-unreachable-branches \
  --txt build/coverage/coverage/coverage.txt \
  --markdown build/coverage/coverage/coverage.md \
  --html-details build/coverage/coverage/index.html \
  --print-summary --fail-under-line 87 --fail-under-branch 75
```

As documented in [tests/README.md](../tests/README.md#coverage), the uncovered
lines in `src/` fall into four categories that are left uncovered on purpose:

1. **Second checks behind `spec.cpp`:** `DescriptorBuilder`,
   `validate_generated`, and `state.cpp` re-check invariants that `spec.cpp`
   already enforces on public inputs (for example Report ID `0` or a mismatched
   generated layout).
2. **64-bit `size_t` arithmetic overflow guards** in `validator.cpp` and
   `descriptor_builder.cpp` that are unreachable on 64-bit hosts where a raw
   descriptor is at most 65,535 bytes and `Report Size` is at most 32.
3. **C++ `catch (...)` boundaries** in `src/api/support.hpp` when every
   allocation along a call path already uses `new (std::nothrow)` or was
   reserved before I/O.
4. **Internal-thread failure races** (`std::thread` launch failure, non-`bad_alloc`
   exceptions inside the event pump, or `libusb_interrupt_event_handler` errors
   during shutdown).

## Known platform differences

Three checks in `tests/sim/` are currently disabled on one platform and tracked
as open items in [tests/README.md](../tests/README.md#known-platform-differences):

| Test | Platform | Observed behavior (open item) |
| --- | --- | --- |
| `aoahid.sim-allocation-failure` (`tests/sim/test_allocation_failure.cpp`) | Windows (MSVC) | Terminates with fast-fail `0xc0000409`; not traced yet (`CMakeLists.txt`). |
| Mouse submit second-allocation failure (`tests/sim/test_hot_path.cpp`) | macOS with `AOAHID_SANITIZE=ON` | `aoahid_node_submit` returns `AOAHID_ERR_TIMEOUT` instead of `AOAHID_ERR_INTERNAL`; not traced yet. |
| Failed report-completion error detail (`tests/sim/test_transport_failures.cpp`) | Windows (MSVC) | `aoahid_last_error()` after a failed request-57 completion reports the expected result code, `transport_status`, and `report_id`, while `aoa_request` and `node_id` are `0`; not traced yet. |
