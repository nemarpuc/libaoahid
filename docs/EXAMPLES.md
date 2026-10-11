# Worked examples

There are deliberately no callable profile presets. The examples show complete
values that can be copied, reviewed for a target, and edited. They explicitly
override every host-transport tuning field for readability even where an
override equals the documented zero-value fallback. Product and target values
must always be set by the caller; the library never infers them.

## Multi-profile lifecycle examples

The comprehensive examples are `examples/c/multi_profile.c`,
`examples/python/multi_profile.py`,
`examples/csharp/MultiProfile/Program.cs`, and
`examples/rust/src/main.rs`. Each exposes `shared` and `threaded` modes.

In `shared` mode, a runtime-sized collection holds every device from one
point-in-time discovery. The example builds one immutable keyboard, mouse, and
touchscreen spec set, registers all three on each opened device, updates all
devices before a second submit pass, closes only the touchscreen, and then uses
the still-live keyboard and mouse. A `NO_DEVICE` result consumes only that
Device, records its caller-owned bus/port/serial locator, re-runs discovery, and
registers the same specs on the matching device while sibling sessions remain
open.

In `threaded` mode, a preliminary context copies one locator per runtime-
discovered device and is then destroyed. Each worker creates and exclusively
drives one context, rediscovers its selected physical identity, and performs the
same lifecycle. No fixed device count appears in storage or scheduling.

The close helpers follow the ABI's ownership rules (see `include/aoahid.h`):

- `aoahid_node_close()` returning `AOAHID_CLOSE_PENDING` retains the Node, so
  the examples poll and retry that same Node.
- the first `aoahid_device_close()` consumes the Device and all its Nodes even
  when it returns `AOAHID_CLOSE_PENDING`; the examples clear those handles and
  let the Context graveyard drain them.
- `aoahid_context_destroy()` is retried only after `AOAHID_CLOSE_PENDING` and a
  poll. Every other result from a valid destroy is treated as terminal and the
  Context is never reused.

## One example per profile kind

`examples/c/profiles/` holds one small, self-contained program for each of the
ten `aoahid_profile_kind` values. Each file carries the complete option set
for its own profile and nothing else, so it can be copied into a product and
edited directly. None of them creates a Context or opens a device, so they run
anywhere the library links. With both `AOAHID_BUILD_EXAMPLES` and
`AOAHID_BUILD_TESTS` on, CTest runs each one as
`aoahid.example-profile-<name>`.

| # | Profile kind | Example file | Factory |
|---:|---|---|---|
| 1 | `AOAHID_PROFILE_KEYBOARD` | `keyboard.c` | `aoahid_spec_create_keyboard` (also reaches the former Barcode/MSR wedge form) |
| 2 | `AOAHID_PROFILE_MOUSE` | `mouse.c` | `aoahid_spec_create_mouse` |
| 3 | `AOAHID_PROFILE_TOGGLE` | `toggle.c` | `aoahid_spec_create_toggle` (Consumer/System/Camera/Telephony, selected by `application_page`/`application_usage`/`field_page`) |
| 4 | `AOAHID_PROFILE_GAMEPAD` | `gamepad.c` | `aoahid_spec_create_gamepad` |
| 5 | `AOAHID_PROFILE_TOUCHSCREEN` | `touchscreen.c` | `aoahid_spec_create_touchscreen` |
| 6 | `AOAHID_PROFILE_PEN` | `pen.c` | `aoahid_spec_create_pen` |
| 7 | `AOAHID_PROFILE_BATTERY` | `battery.c` | `aoahid_spec_create_battery` |
| 8 | `AOAHID_PROFILE_RAW` | `raw.c` | `aoahid_spec_create_raw` |
| 9 | `AOAHID_PROFILE_TOUCHPAD` | `touchpad.c` | `aoahid_spec_create_touchpad` |
| 10 | `AOAHID_PROFILE_DIGITIZER` | `digitizer.c` | `aoahid_spec_create_digitizer` |

`profile_example.h` is shared by all ten. It holds only the reporting
helper; it contains no product values, so each example remains the single
source of its own options. That helper prints two things: the descriptor size
and report layout the library derived from the caller's options, and then the
complete call sequence that would send the profile to a real device -
`aoahid_context_create`, `aoahid_discover`, `aoahid_device_open`,
`aoahid_node_open`, the profile's own mutation calls, `aoahid_node_close`, and
`aoahid_device_close`. Each example supplies its own mutation steps, so
`keyboard.c` shows the `aoahid_kbd` press/submit/release/submit cycle while
`touchscreen.c` shows the `aoahid_touch` tip-down/tip-up frame, and
`touchpad.c` shows the same `aoahid_touch` frame plus an
`aoahid_touchpad_button` press/submit/release/submit cycle. The sequence is
printed rather than executed because these programs open no device; run
`examples/c/verify/` against a phone to watch the same calls take effect.

The option values match those in `tests/hidtools/export_descriptors.cpp`,
reduced to one representative form per kind. The expected target-event strings
in the toggle allow-list are placeholders: a product should use the event names
it actually observes on its own target.

The smaller enumerate-only examples remain available at
`examples/python/enumerate.py`, `examples/csharp/Program.cs`, and
`bindings/rust/aoahid-sys/examples/enumerate.rs`. They print bus, current USB
address, physical port path where the comprehensive form is used, VID/PID,
product, and serial exactly as the discovery snapshot reports them.

## Human-observable real-device verification programs

`examples/c/verify/` is a third category: unlike `multi_profile.c` (an
automated CTest exercise that finishes in milliseconds) and
`examples/c/profiles/` (Spec-only, never opens a device), each program under `verify/` opens a real device and
produces a sustained effect a person can actually confirm -- typing a message
repeatedly (`verify_keyboard.c`), dragging across the screen five times
(`verify_touch.c`), moving the cursor in a circle for several seconds
(`verify_mouse.c`), and firing four Consumer Control Usages in turn --
Play/Pause, Volume Increment, Mute, "AC New" -- for a media app to react to
(`verify_toggle.c`). `verify_all.c` runs touch, keyboard, and mouse together,
and `verify_accessory.c` switches the phone to accessory mode first.
`verify_battery.c` is the one exception to "sighted on
the phone's screen": Battery Strength is kernel `power_supply` metadata, not
an input event, so confirming it means checking `adb shell dumpsys battery`
and `/sys/class/power_supply` instead, as its own comment header explains.
They print what to do before they start (for example, focus a text field)
and print a plain-language error instead of failing silently.
`docs/QUICKSTART.md` is the full walkthrough these programs are built for;
`CMakeLists.txt` builds all seven whenever `AOAHID_BUILD_EXAMPLES` is on, but
never registers them as CTest cases, since they need a physical phone and
are meant to be watched rather than run unattended.

## Example values

Every number below is visible in each comprehensive example and is selected by
that caller. A zero-valued native tuning field would instead select the bounded
fallback recorded in `API.md`; these examples do not rely on that shorthand.

| Example value | Role |
|---|---|
| 500 ms | Control, send, poll, and blocking-submit budget selected for the example. Control/send happen to equal the native zero fallback; poll and blocking-submit have separate explicit arguments. A timeout is a failure deadline, not added success latency. |
| 1000 ms | Device close-drain budget, explicitly matching the native zero fallback. |
| 64 bytes | Descriptor fragment policy, explicitly matching the native zero fallback. The official AOA page fixes ordering and offset, not a fragment size. |
| 8 pool slots; 1 reserved slot per Node | Capacity and reservation choices for this three-profile example. Eight matches the Device fallback; the one-slot Node reservation is explicit because zero/zero means no reservation. |
| First-report STALL | The library never retries. The multi-profile examples report a STALL once and send again on their next loop iteration, because a refused report stays pending; `examples/c/verify/verify_common.h` resends explicitly up to 20 times, 1 ms apart. |
| 4096 descriptor/target policies | Descriptor-size limits chosen by the example. The Linux HID core rejects descriptors larger than `HID_MAX_DESCRIPTOR_SIZE` (4096, `include/linux/hid.h`). They are not universal AOA limits. |
| 4088 host/report policy | Deliberately conservative host control-buffer value. It overrides the 1024-byte `maximum_report_bytes` fallback and is required as `host_control_buffer_policy_bytes`. |
| 256 fields, stack depth 4, 12288 usages, 65528 report-data bits, 256 report-size bits | Linux HID core parser limits (`include/linux/hid.h`, `drivers/hid/hid-core.c`) for the kernel these examples target; check them against your target kernel. |
| Keyboard `0x04` through `0x65` | Keyboard a/A through Keyboard Application comes from HUT 1.7 §10; the Usage interval is the only caller choice, since every declared key is a one-bit Variable field. |
| Mouse axes -127 through 127 at 8 bits; 3 buttons | Complete signed relative range and profile geometry chosen by the caller. |
| One-contact MT; Contact ID 0 through 15 at 4 bits; coordinates 0 through 32767 at 16 bits | A complete one-contact example geometry; none of these ranges is claimed as an Android or HID universal value. |
| Key A, mouse delta `(30,-20)`, contact `(1000,2000)`, pointer button 1 | Input performed solely to make every state/submit pass observable. |

`interface_number=-1` accompanies the explicit no-claim policy. Every disabled
optional field is explicitly zero, and every required field is assigned. The
immutable specs stay alive through reconnection and are released only after all
Nodes have either closed or been handed to Device teardown.

Build it by enabling `AOAHID_BUILD_EXAMPLES`, or compile it against an installed
CMake package target `aoahid::aoahid`. With no requested component, an installed
package selects `shared` when present and otherwise selects `static`.
`find_package(aoahid CONFIG REQUIRED COMPONENTS shared)` loads only the shared
export. `COMPONENTS static` loads only the static export and resolves libusb;
the explicit static target is always `aoahid::aoahid_static`. A static-only
installation also provides `aoahid::aoahid` as a compatibility/default alias.
Both components can be requested together.

Release archives also install `lib/pkgconfig/aoahid.pc`. Shared metadata has no
private libusb dependency. Static metadata declares libusb and, on toolchains
that expose it as an implicit link library, the compiler-reported C++ runtime
needed when a C compiler links `libaoahid.a`; the static archive includes a
relocatable `libusb-1.0.pc` for that dependency.

## Creating an option structure

Use zero initialization as storage initialization, then assign every product
and target field whose meaning applies. A Device tuning field may deliberately
remain zero to request its documented fallback; the comprehensive examples set
all eight explicitly. An optional disabled product field must have its enable
flag set to zero and its associated field storage left as the explicitly unused
zero form. Example:

```c
aoahid_mouse_options mouse = {0};
mouse.struct_size = sizeof mouse;
mouse.reserved = 0;
mouse.report_id = (aoahid_report_id_option){0, 0, {0, 0, 0}};
mouse.button_count = 3;
mouse.x = (aoahid_integer_field){-127, 127, 8};
mouse.y = (aoahid_integer_field){-127, 127, 8};
mouse.enable_wheel = 0;
mouse.wheel = (aoahid_integer_field){0, 0, 0};
mouse.enable_pan = 0;
mouse.pan = (aoahid_integer_field){0, 0, 0};
```

The ranges above are a caller selection. The factory proves that relative axes
can represent negative, zero, and positive values; it does not choose them.

## Current USB mode and the AOA version

Every example opens the device in its current USB mode. `aoahid_device_open`
sends no AOA request: no `GET_PROTOCOL`, identification strings, audio request,
or `ACCESSORY_START`, and it does not wait for a new VID/PID. A physical port
path remains useful for stable discovery identity.

Discovery reports each device's AOA version in `protocol_version`. The library
applies no version policy, so each example prints the version and refuses to
open a device below version 2 with `AOAHID_ERR_NOT_AOA`.

## Bindings

- Python: call `aoahid.load(explicit_path)` and populate `ctypes.Structure`
  product/target fields yourself. Zero-valued Device tuning fields are handled
  by the native API. The binding test proves `struct_size` begins at zero.
- C#: populate the sequential structs, using unmanaged UTF-8 storage whose
  lifetime covers the native call. P/Invoke uses C calling convention.
- Rust: populate `aoahid_sys` `repr(C)` structures. The safe wrapper owns specs
  and provides a typed keyboard-node reference, but does not construct options.

All bindings expose native result codes. On error, inspect the C thread-local
detail on the same calling thread.

## Threading references

Official documentation for the threading calls the examples use:

| Scope | Official source | Section or symbol used |
|---|---|---|
| POSIX hosts | https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_create.html and https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_join.html | `pthread_create`, `pthread_join` |
| Windows hosts only | https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/beginthread-beginthreadex?view=msvc-170, https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread, and https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject | `_beginthreadex` for a worker that uses the CRT; `WaitForSingleObject` and `CloseHandle` for its returned handle |
| Python 3.9 minimum and CI's Python 3.13 | https://docs.python.org/3.9/library/threading.html and https://docs.python.org/3.13/library/threading.html | `threading.Thread.start`, `Thread.join`, non-daemon cleanup |
| .NET | https://learn.microsoft.com/en-us/dotnet/api/system.threading.thread.start?view=net-8.0 and https://learn.microsoft.com/en-us/dotnet/api/system.threading.thread.join?view=net-8.0 | `Thread.Start`, `Thread.Join` |
| Rust | https://doc.rust-lang.org/std/thread/fn.scope.html | `std::thread::scope` and joined scoped workers |

The published Python wheel and NuGet package contain declarations only. The
Python caller passes the exact shared-library path to `aoahid.load`; the .NET
native loader must resolve the name `aoahid`. In both cases, install the
version-matched shared native archive and its libusb runtime separately.
