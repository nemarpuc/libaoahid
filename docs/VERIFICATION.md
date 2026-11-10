# Verification

This page documents what is verified in `libaoahid`, which verification layer
covers each feature, the exact hardware-verification status, line and branch
coverage numbers over `src/`, and known platform-specific open items.

For how to run and extend the test suites, see
[tests/README.md](../tests/README.md#tests). For running the hardware checks on a
physical Android phone, see [tests/hw/README.md](../tests/hw/README.md#hardware-checks),
[tools/device-check/README.md](../tools/device-check/README.md#device-check), and
[TARGET_MATRIX.md](TARGET_MATRIX.md).

## Terms

The two sister libraries use some of the same words for different things:

| Term | In `libaoahid` | In `libmimhid` ([its VERIFICATION.md](https://github.com/nemarpuc/libmimhid/blob/main/docs/VERIFICATION.md)) |
|---|---|---|
| `unit` | In-process tests with no USB transport at all (`aoahid.unit` and the other gates of layer 1 below) | In-process tests with no serial port and no board |
| `scripted` | — (no such layer) | The host library over a pseudo-terminal against a peer the test drives frame by frame |
| `sim` | The public C ABI and `src/transport/` against the fake libusb in `tests/fake_libusb/` (`-DAOAHID_USE_FAKE_LIBUSB=ON`) | The host library over a pseudo-terminal against the real firmware core built for the build host |
| `hw` | A real Android phone over a USB cable ([tests/hw/README.md](../tests/hw/README.md#hardware-checks)) | A physical board, a USB-UART adapter, and a target computer |
| hardware-verified | A release whose CHANGELOG section says it ran against a real Android phone | A release whose library and firmware ran on a physical board |

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

The `hw` layer has no per-feature column: what ran on a phone is recorded only
in [CHANGELOG.md](../CHANGELOG.md), summarized under
[Hardware verification status](#hardware-verification-status).

| Feature / area | `unit` (`tests/unit/`) | `sim` (`tests/sim/`, fake libusb) |
| --- | --- | --- |
| HID 1.11 short-item encoding (`ItemWriter`) | `test_item_writer.cpp` (`item_writer`), `fuzz_descriptor.cpp` | Exercised through all profile and session tests |
| Generated descriptor structure & golden bytes (`DescriptorBuilder`) | `test_profiles.cpp`, `tests/golden/*.hex` (`aoahid.golden-descriptors`), `hid-tools` CI gate | `test_session_setup.cpp` |
| Profile option validation (`aoahid_*_options_init`, `aoahid_spec_create_*`) | `test_spec_options.cpp` (`spec_options`), `symbol_lint.py` zeroed-struct gate | `test_api_arguments.cpp` (`api_arguments`) |
| Keyboard profile (NKRO bitmap + modifiers) | `test_profiles_keyboard.cpp` (`profiles`), `keyboard.hex` | `test_session_setup.cpp`, `test_submit.cpp`, `test_report_state.cpp`, `test_neutral_close.cpp` |
| Mouse profile (buttons, X/Y deltas, wheel, AC Pan) | `test_profiles_mouse.cpp` (`profiles`), `mouse-wheel-pan.hex` | `test_submit.cpp`, `test_report_state.cpp`, `test_neutral_close.cpp` |
| Touchscreen profile (Fixed-slot multi-touch, Scan Time, optional geometry/pressure) | `test_profiles_touch.cpp` (`profiles`), `touchscreen-fixed-mt.hex` | `test_touch_battery.cpp`, `test_report_state.cpp`, `test_neutral_close.cpp` |
| Touchpad profile (`INPUT_PROP_POINTER`, contacts + external buttons) | `test_profiles_touch.cpp` (`profiles`), `touchpad.hex` | `test_touch_battery.cpp`, `test_report_state.cpp`, `test_neutral_close.cpp` |
| Pen profile (`INPUT_PROP_DIRECT`, hover, tip, barrel, eraser, invert, pressure, tilt) | `test_profiles_pen.cpp` (`profiles`), `pen.hex` | `test_report_state.cpp`, `test_neutral_close.cpp` |
| Digitizer profile (`INPUT_PROP_POINTER`, indirect tablet) | `test_profiles_pen.cpp` (`profiles`), `digitizer.hex` | `test_report_state.cpp`, `test_neutral_close.cpp` |
| Gamepad profile (Hat Switch, D-pad buttons, or no D-pad; buttons; up to 9 axes) | `test_profiles_gamepad.cpp` (`profiles`), `gamepad.hex` | `test_report_state.cpp`, `test_neutral_close.cpp` |
| Toggle profile: Consumer Control (`0x0C`) | `test_profiles_toggle.cpp` (`profiles`), `consumer-controls.hex` | `test_report_state.cpp`, `test_neutral_close.cpp` |
| Toggle profile: System Control (`0x01`), Camera (`0x90`), Telephony (`0x0B`) | `test_profiles_toggle.cpp` (`profiles`), `system-controls.hex`, `camera-keys.hex`, `telephony-keys.hex` | `test_report_state.cpp` |
| Battery report (`enable_battery`, `aoahid_battery_update`) | `test_profiles_battery.cpp` (`profiles`), `keyboard-battery.hex`, `examples/c/profiles/keyboard_battery.c` | `test_touch_battery.cpp`, `test_report_state.cpp` |
| Validated raw descriptor (`AOAHID_PROFILE_RAW`, `aoahid_raw_submit`) | `test_raw_validator.cpp` (`raw_descriptor`), `test_profiles_raw.cpp` (`profiles`), `raw-validated.hex`, `fuzz_descriptor.cpp` | `test_session_setup.cpp`, `test_submit.cpp`, `test_report_state.cpp` |
| USB discovery (`aoahid_discover`, request 51 `ACCESSORY_GET_PROTOCOL`) | — | `test_identity.cpp` (`identity`), `test_transport_failures.cpp` (`transport_failures`) |
| Accessory mode switch (`aoahid_accessory_start`, requests 51–53) | — | `test_accessory_start.cpp` (`transport`), `test_transport_failures.cpp` (`transport_failures`) |
| Device open, Linux HID policy check & shared `Port` (`aoahid_device_open`) | — | `test_device_open.cpp`, `test_session_setup.cpp`, `test_transport_failures.cpp` |
| Node registration, descriptor slicing & close (`aoahid_node_open`, `aoahid_node_close`, requests 54, 56, 55) | — | `test_session_setup.cpp`, `test_node_close.cpp`, `test_neutral_close.cpp`, `test_transport_failures.cpp` |
| Report submission, transfer pool & reservations (`aoahid_node_submit`, `aoahid_node_submit_blocking`, request 57) | — | `test_submit.cpp`, `test_hot_path.cpp`, `test_internal_thread.cpp`, `test_transport_failures.cpp` |
| Bulk Channels (`aoahid_channel_open`, `read`, `write`, `close`; `STREAM` & `REQUEST` modes; ZLP) | — | `test_channel.cpp`, `test_internal_thread.cpp`, `test_transport_failures.cpp` |
| Event modes, teardown & Context graveyard (`AOAHID_EVENT_CALLER_POLL`, `AOAHID_EVENT_INTERNAL_THREAD`, `aoahid_device_close`, `aoahid_context_destroy*`) | — | `test_transport_runtime.cpp`, `test_context_destroy.cpp`, `test_internal_thread.cpp` (`aoahid.sim-internal-thread`) |
| Context serialization check (`AOAHID_CHECK_SERIALIZATION`) | — | `test_serialization_check.cpp` (`aoahid.sim-serialization-check-ok`, `aoahid.sim-serialization-check-violation`) |
| Heap allocation failure resilience (`std::nothrow` & C ABI catch firewall) | — | `test_allocation_failure.cpp` (`aoahid.sim-allocation-failure`), `test_hot_path.cpp` |
| Public C ABI, C++ header & bindings (Python, C#, Rust) | `test_c_abi.c` (`aoahid.c-abi`), `test_cpp_wrapper.cpp` (`aoahid.cpp-wrapper`), `check_abi_golden.py` (`aoahid.abi-layout-oracle`), `symbol_lint.py`, `bindings/*/tests` | `examples/python/enumerate.py`, `examples/csharp/Enumerate.csproj`, `bindings/rust/aoahid-sys/examples/enumerate.rs` |
| CMake package, `pkg-config` & release archives | `tests/cmake/*`, `tests/release/test_release_tools.py` | `tools/release/consumer` (`shared`, `static`, `shared;static`) |

## Hardware verification status {#hardware-verification-status}

As recorded in the CHANGELOG section for 0.1.0 (2026-11-07):

- **Release `0.1.0` itself is not hardware-verified** on a phone in its
  current form; the API around the transport and the profiles has changed
  since they last ran on a device.
- **What was verified before that:** the transport and the Keyboard, Mouse,
  Toggle, Gamepad, Touchscreen, Touchpad, Pen and Digitizer profiles, on a
  Samsung Galaxy Tab S11 from an Arch Linux host, through devtest and kernel
  `getevent`.
- **What the current library is tested with:** the fake libusb backend
  (including a ThreadSanitizer build), and the bindings against the compiled C
  ABI.

Everything else (the Raw profile, the battery report, other devices, Windows,
macOS and AArch64 hosts) has no hardware result recorded; see
[TARGET_MATRIX.md](TARGET_MATRIX.md#not-recorded-on-hardware).

## Coverage {#coverage}

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

## Known platform differences {#known-platform-differences}

Three checks in `tests/sim/` are currently disabled on one platform and tracked
as open items in [tests/README.md](../tests/README.md#known-platform-differences):

| Test | Platform | Observed behavior (open item) |
| --- | --- | --- |
| `aoahid.sim-allocation-failure` (`tests/sim/test_allocation_failure.cpp`) | Windows (MSVC) | Terminates with fast-fail `0xc0000409`; not traced yet (`CMakeLists.txt`). |
| Mouse submit second-allocation failure (`tests/sim/test_hot_path.cpp`) | macOS with `AOAHID_SANITIZE=ON` | `aoahid_node_submit` returns `AOAHID_ERR_TIMEOUT` instead of `AOAHID_ERR_INTERNAL`; not traced yet. |
| Failed report-completion error detail (`tests/sim/test_transport_failures.cpp`) | Windows (MSVC) | `aoahid_last_error()` after a failed request-57 completion reports the expected result code, `transport_status`, and `report_id`, while `aoa_request` and `node_id` are `0`; not traced yet. |
