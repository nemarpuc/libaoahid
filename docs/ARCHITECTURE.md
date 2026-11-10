# Architecture

This document describes how libaoahid is put together. It is for contributors
and for users who need to reason about threading, lifetimes, or failure
handling. For the wire protocol see [PROTOCOL.md](PROTOCOL.md); for the full
function reference see [API.md](API.md); for the implementation behind it
(locks, the send path, teardown) see [INTERNALS.md](INTERNALS.md).

## Layers

```
application
   │  C ABI (include/aoahid.h)  or  C++ header (include/aoahid.hpp, namespace aoahid)
   ▼
src/api        ABI boundary: argument/struct validation, handle lifetimes, error records
src/profiles   immutable Spec per profile + per-Node report state machines
src/hid        HID report descriptor writer, builder, validator
src/transport  libusb: discovery, AOA control requests, report transfer pool, bulk Channels
   │
   ▼
libusb 1.0 (runtime 1.0.30 or later is required)
```

No libusb or C++ type crosses the public header. `src/transport/transport.hpp`
is the internal boundary: the layers above it see only plain host-side values.

## Object model

| Object | Created by | Owns / borrows |
| --- | --- | --- |
| Context | `aoahid_context_create` | The libusb context, the event mode, the optional event thread, and a graveyard for teardown that could not finish in time. |
| Discovery | `aoahid_discover` | A snapshot of devices that answered AOA request 51 with a nonzero version. Independent lifetime. |
| Device | `aoahid_device_open` | One opened USB device in its current mode, a fixed transfer pool, and optionally one claimed interface. |
| Spec | `aoahid_spec_create_*` | An immutable, reference-counted profile description and its generated HID report descriptor. Not tied to a Context. |
| Node | `aoahid_node_open(device, spec, ...)` | One AOA HID ID registered on the Device for one Spec, plus that Node's report state. Retains its Spec. |
| Channel | `aoahid_channel_open` | A bulk IN/OUT interface on the same USB handle as the Device (used, for example, for ADB). |

Relationships: Context → Device → { Node…, Channel… }. Closing a Device closes
every Node and Channel still open on it; destroying a Context tears down every
child graph.

Internally a USB handle is held by a reference-counted `Port`. A Device and its
Channels borrow the same Port, and discovery on an already open device probes
through that Port instead of opening a second handle (a second open fails under
WinUSB). Interface claims are counted per interface.

### HID IDs

AOA HID IDs are chosen by the host. The library allocates them from a
monotonic counter starting at 1, kept per physical identity (USB bus plus port
path) within a Context, so IDs are not reused while the Context lives, even if
the device re-enumerates. Exhausting the 16-bit space returns
`AOAHID_ERR_OVERFLOW`. `aoahid_node_id` returns the ID of a Node.

## Event modes and threading

The mode is fixed at `aoahid_context_create`:

| Mode | Who drives libusb events | Locking |
| --- | --- | --- |
| `AOAHID_EVENT_CALLER_POLL` | The caller, through `aoahid_context_poll`. Transfer callbacks run inside that call. No library thread is created (the platform libusb backend may still own helper threads). | No internal send-path lock. |
| `AOAHID_EVENT_INTERNAL_THREAD` | One library event thread. | Pool and error-latch access is synchronized, which adds contention that caller-poll mode avoids. |

Serialization rules, from the header contracts:

- Every application call that touches one Context (including its Devices,
  Nodes, and Channels) must be serialized by the caller. The library does not
  make concurrent application calls on one Context safe, in either mode.
- Separate Contexts are independent.
- Spec objects are immutable; concurrent reads are allowed while each reader
  holds a reference (`aoahid_spec_retain` / `aoahid_spec_release`).
- `aoahid_last_error` is thread-local and valid until that thread's next call.
- Never call close/destroy functions from the log sink.

### Checking the serialization rule {#checking-the-serialization-rule}

Breaking the first rule is undefined behavior that usually shows up far from
its cause. A library built with `-DAOAHID_CHECK_SERIALIZATION=ON` (off by
default; on in the `dev` preset) ends the process, with a line on stderr that
names the second call (the function, or for the report-state calls their
field name, such as `mouse.move`), when a thread enters a call on a Context
while another thread is inside one. It is a debugging aid, not a lock: it adds
an atomic exchange to every checked call and is not meant for release builds,
whose code it does not change when off. It is left out of the `tsan` preset:
its own atomic orders the calls of different threads, which would hide from
ThreadSanitizer the unsynchronized calls that happen not to overlap.

It checks the calls whose contract in `aoahid.h` says they must be serialized
with every other call on the Context. It does not check:

- `aoahid_channel_read` and `aoahid_channel_write` in internal-thread mode
  (one reader and one writer may run beside other calls there), or a Channel
  read racing its own close;
- `aoahid_node_id` and `aoahid_node_manifest`, which only have to be kept
  apart from the Node's teardown;
- the Spec functions and the discovery snapshot readers;
- a call on a null or closed handle;
- a call made from inside another call on the same thread (the log sink must
  not call into its Context, and this does not catch it);
- a call that enters after `aoahid_context_destroy` or
  `aoahid_context_destroy_blocking` has started: those two only look, on
  entry, for a thread already inside.

## Send path

Each Node has at most one report in flight.

1. **Update.** A profile call (`aoahid_keyboard_key`, `aoahid_touchscreen_contact`, `aoahid_mouse_move`,
   `aoahid_gamepad_axis`, …) changes the Node's state. It does not block,
   allocate, log, or do I/O. It returns `AOAHID_ERR_BUSY` while a report (or a
   multi-packet touch frame) is in flight.
2. **Submit.** `aoahid_node_submit` serializes the dirty state directly into a
   slot of the Device's transfer pool and submits it as an asynchronous AOA
   request 57 control transfer. It does not wait for completion; in caller-poll
   mode it may run one zero-timeout event pump after acceptance.
   `aoahid_node_submit_blocking` additionally waits, up to an explicit timeout,
   until every dirty report has completed. `aoahid_raw_submit` copies caller
   bytes for a Raw Node.
3. **Completion.** The libusb callback (in `aoahid_context_poll` or on the event
   thread) returns the slot to the pool. A failure is latched on the Device and
   reported by the next call or by `aoahid_device_latched_error`.

With `validate_reports = 1` in `aoahid_device_options`, each generated report
is decoded again against the Spec before request 57 is sent.

### Transfer pool

`aoahid_device_open` allocates a fixed number of slots
(`transfer_pool_slots`). Each slot holds one libusb transfer and a buffer of
the 8-byte setup packet plus `maximum_report_bytes`. The setup fields that
never change are written once, at open (`libusb_fill_control_setup`); each
submission stores only the HID ID (wValue) and the report length (wLength).
No allocation happens on the send path.

The pool is shared by every Node on the Device. A Node can reserve one slot
(`aoahid_node_options.has_reserved_slots` / `reserved_slots`, zero or one) so
other Nodes cannot starve it; reservations must always leave at least one
shared slot. When no usable slot is free, submission returns `AOAHID_ERR_BUSY`.

### Registration and close

`aoahid_node_open` synchronously sends request 54 (register) and the
descriptor in request 56 fragments of `descriptor_fragment_bytes`. If
registration fails after the device may have accepted request 54, a
best-effort request 55 is sent so no half-registered ID is left behind.

`aoahid_node_close` waits up to its `drain_timeout_ms` argument (zero is
`AOAHID_ERR_UNSET_FIELD`) for in-flight work, finishes any touch frame, sends the neutral
reports the profile requires (every key and button up, contacts lifted, axes at
rest), and then sends request 55. A failed step does not stop the next one. If
a report is still in flight at the deadline, the Node close returns
`AOAHID_CLOSE_PENDING` and the Node stays the caller's; a Device close instead
moves the consumed graph to the Context graveyard, which reaps it once the
callbacks have run.

## Design decisions {#design-decisions}

Each decision has the same three lines as in libmimhid's ARCHITECTURE.md.
**Reason** quotes the code or a commit message; where neither records one, it
says so. The version numbers in the cited commit subjects (1.0.1, 2.0.0, 4.0.5
and so on) predate the restart of the version line at 0.1.0 (commit
`5e9e8a7`); the commits are in this repository's history.

### Zero inference {#zero-inference}

**Decision:** Usages, logical ranges, bit widths, Report IDs, Linux HID parser
limits, timeouts, fragment sizes, pool sizes and Channel transfer values are
caller inputs. A required field or timeout left zero is
`AOAHID_ERR_UNSET_FIELD` (or `AOAHID_ERR_PARAM`) before any USB I/O, and
every enumeration starts at `1`, so a zeroed enum field is refused too. The
`aoahid_*_options_init` functions and the `AOAHID_*_TIMEOUT_MS` constants
give one working set of values the caller can read and change; the library
never puts its own value in place of a zero.

**Reason:** Commit `7c2ffdc` ("tuning values have no defaults") made zero
`AOAHID_ERR_UNSET_FIELD` "instead of a library-chosen value"; commit
`c82dc1c` moved every timeout into the arguments of the call that waits, "as
in libmimhid"; commit `0e48856` added the initializers and constants. Why a
library-chosen value was unwanted is not recorded beyond these messages.

**Rejected:** Zero selecting a library-chosen value in `aoahid_device_open`,
`aoahid_channel_open` or the profile factories (`7c2ffdc`), and timeouts kept
in `aoahid_device_options` (`c82dc1c`).

### No convenience names

**Decision:** No exported function name contains `default`, `preset`,
`standard` or `typical` as an underscore-separated word.
`tools/symbol-lint/symbol_lint.py` checks the names (`BANNED_CONVENIENCE_RE`)
and calls `aoahid_context_create` and every `aoahid_spec_create_*` with an
all-zero options structure, failing if one accepts it.

**Reason:** Not recorded in a commit message; the check enforces
[Zero inference](#zero-inference) on the names.

**Rejected:** Factory functions in the C ABI that pick product or target
values behind such a name.

### One immutable Spec, shared by Nodes

**Decision:** `aoahid_spec_create_*` builds the report descriptor and its
`ReportLayout` in one pass (`DescriptorBuilder` in
`src/hid/descriptor_builder.cpp`), validates them before any USB I/O, and
returns an immutable `aoahid_spec` whose atomic `references` count each Node
that uses it raises. Nodes on different Devices can share one Spec.

**Reason:** Not recorded. The code says only that the builder makes "a report
descriptor and its input layout in the same pass"
(`src/hid/descriptor_builder.hpp`).

**Rejected:** None recorded.

### Two event modes

**Decision:** Each Context picks `AOAHID_EVENT_CALLER_POLL` (no library
thread; `active_state_mutex` and `active_completion_mutex` are null, so
`ModeMutexGuard` takes no lock) or `AOAHID_EVENT_INTERNAL_THREAD` (one event
thread runs `Runtime::poll` with a 60-second bound, and shared state is under
`context->mutex` and `node->completion_mutex`).

**Reason:** Why both modes are offered is not recorded. The 60-second bound is
explained in `src/api/c_api_context.cpp`: "A long bounded wait removes the
former 100 Hz idle wake cadence while still bounding shutdown if a backend
cannot honor an event-handler interrupt." Commit `1c6b080` removed an atomic
flag that let `aoahid_node_submit` skip `context->mutex`: it "saved one
uncontended lock per report, which was never measured, and had to be kept in
step with the error latch by hand."

**Rejected:** A 10 ms idle poll on the event thread (the code comment above),
and the atomic flag (`1c6b080`).

### A fixed transfer pool

**Decision:** `aoahid_device_open` allocates `transfer_pool_slots` libusb
transfers with buffers of `8 + maximum_report_bytes` bytes, and a Node may
reserve one slot at `aoahid_node_open` so other Nodes cannot take every slot.
Free slots are kept in a `std::vector<Slot*>` used as a stack
(`free_stack` in `src/transport/transport.cpp`). With every slot taken,
`aoahid_node_submit_blocking` waits on a Device-level condition variable that
each completion signals.

**Reason:** The send path allocates nothing ([LATENCY.md](LATENCY.md)). Commit
`97bbd17` replaced a 1 ms recheck with the condition variable so a blocked
submit "sends as soon as a slot is free and uses no CPU while waiting". Why
the free slots are taken last-in first-out is not recorded.

**Rejected:** Allocating a transfer or buffer per report, and the 1 ms recheck
(`97bbd17`, `aa3dbf9`).

### No retry or re-enumeration wait

**Decision:** Every AOA request is sent once. When request 57 completes with
`AOAHID_ERR_STALL`, the refused state stays pending (`node->dirty`,
`TouchState::packet_cursor` and `scan_time` are kept), so the caller's next
submit sends it again. `aoahid_accessory_start` sends requests 51 to 53 and
returns without waiting for the new enumeration, and `aoahid_device_open`
sends no request.

**Reason:** Commit `b12b7d4`: "the library never retries a transfer ...
Because f_accessory stalls request 57 when the HID ID is not registered yet
(nothing is applied), the refused state stays pending and the caller's next
submit resends it." Commit `0f7739e` reverted a `LIBUSB_TRANSFER_ERROR` retry
because it was "unconfirmed against actual WinUSB or hub behavior". Commit
`ef7b3d1` made open send no probe.

**Rejected:** The automatic resend of the first report after a STALL
(`first_report_attempts`, `first_report_backoff_us`; `b12b7d4`), the
`LIBUSB_TRANSFER_ERROR` retry (`0f7739e`), and an automatic re-enumeration
design (`Link`), whose design record commit `e1ba9ba` removed as rejected.

### The Context is the serialization domain, and one Port per USB device

**Decision:** Calls on one Context and its children must be serialized by the
caller (checked with `-DAOAHID_CHECK_SERIALIZATION=ON`); several devices run
in parallel with one Context each. Within a Context, a reference-counted
`transport::Port` (`src/transport/port.cpp`) owns the one
`libusb_device_handle` of a physical port and shares it between the Device,
its Channels, discovery and accessory start; it counts claims per interface.

**Reason:** A second open of the same device fails under WinUSB (commit
`b12b7d4`). Commit `553e774`: libusb's WinUSB backend sends a control transfer
"through a WinUSB interface it claims for that transfer and releases on
completion", which undid a Channel claim made while a report was in flight, so
`aoahid_channel_open` and `aoahid_channel_close` wait until no report is in
flight. Why the Context, and not the Device, is the serialization unit is not
recorded.

**Rejected:** A second handle for the same device (`b12b7d4`), and claiming a
Channel interface while a report transfer is in flight (`553e774`).

### A C ABI with C++ forwarders and generated bindings

**Decision:** The binary boundary is 53 C functions with
`struct_size`-prefixed option structures and opaque handles; exceptions stop at
the boundary (`AOAHID_C_*_CATCH` in `src/api/support.hpp`) and the C++
runtime is linked statically. `include/aoahid.hpp` forwards every function
under namespace `aoahid` and adds the option functions
(`aoahid::device_options` and the others) and `SpecPtr` / `DiscoveryPtr`.
`tools/bindings/generate.py` writes the Python, C# and Rust declarations from
`include/aoahid.h`.

**Reason:** Commit `0581391`: the header "now forwards every function of
aoahid.h unchanged". Commit `24c2092` replaced the hand-written declarations
with the generator, shared with libmimhid. Commit `9d91bca` linked the C++
runtime statically and records no reason.

**Rejected:** Typed C++ node references and `aoa::bind` (removed in
`0581391`), and hand-maintained per-language declarations (`24c2092`).

### NKRO keyboard and Game Pad collection only

**Decision:** The Keyboard profile emits a one-bit-per-Usage Variable bitmap
only, and the Gamepad profile always emits the Generic Desktop Game Pad
Application Collection.

**Reason:** Commit `25789c0` removed the Array (6KRO) form: with the bitmap
"any number of simultaneously pressed keys up to the declared range is
reported at once, so there is no Array slot count and no ErrorRollOver (0x01)
overflow encoding". Commit `c05aec4` removed the Joystick collection after a
source audit: Linux `hid-input.c` maps Joystick buttons to `BTN_JOYSTICK` and
Game Pad buttons to `BTN_GAMEPAD`
([hid-input.c](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/hid/hid-input.c?h=v6.18)),
AOSP `Generic.kl` maps only codes 304 and up to the named `BUTTON_A`, `BUTTON_B`
and following keycodes and 288 to 303 to `BUTTON_1` to `BUTTON_16`
([Generic.kl](https://android.googlesource.com/platform/frameworks/base/+/1cdfff555f4a21f71ccc978290e2e212e2f8b168/data/keyboards/Generic.kl)),
and `EventHub.cpp` classifies a device as a gamepad when it has any of the
gamepad keycodes (`openDeviceLocked` in
[EventHub.cpp](https://android.googlesource.com/platform/frameworks/native/+/4f463a6b1de9198963dc6aff74154a504ba3f8f6/services/inputflinger/reader/EventHub.cpp));
all retrieved 2026-11-10.

**Rejected:** The Array keyboard with ErrorRollOver (`25789c0`) and the
Joystick Application Collection (`c05aec4`).

## Error model

- Every fallible function returns `aoahid_result` (`int32_t`). `AOAHID_OK` is
  0; errors are the `AOAHID_ERR_*` constants; `AOAHID_CLOSE_PENDING` means a
  close could not finish within its budget and the handle is still the
  caller's. Every close and destroy follows the same ownership rule
  ([API.md](API.md#close-and-destroy)).
- `aoahid_last_error()` returns a thread-local `aoahid_error_detail`: code,
  the field name, a reason string, the raw libusb status, the AOA request
  number, HID ID, Report ID, and offset/length where relevant.
- `aoahid_result_name()` maps a code to its name.
- Completion errors are latched per Device. `AOAHID_ERR_NO_DEVICE` is sticky;
  other latched errors are consumed when reported.
- C++ exceptions never cross the ABI. Allocation failure and unexpected
  exceptions become `AOAHID_ERR_INTERNAL`.

## ABI stability

- All public enums are `int32_t` typedefs with anonymous-enum constants, so
  struct layout does not depend on compiler enum sizing.
- Every options struct starts with `uint32_t struct_size`, which must equal
  `sizeof` the struct in the release the caller links against; a mismatch
  returns `AOAHID_ERR_PARAM`. Most also carry a `reserved` field that must be
  zero.
- Handles are opaque pointers. The `*_init` functions are `static inline` in
  the header, so they add nothing to the binary ABI.
- `AOAHID_VERSION_MAJOR/MINOR/PATCH` and `aoahid_version()` report the version.
- Layout is guarded by `tests/abi` (C and C++ ABI tests, a layout oracle, and
  `check_abi_golden.py`) and by the generated bindings: the Python, C#, and
  Rust declarations come from `aoahid.h` through `tools/bindings/generate.py`,
  and each language compares its layout with the oracle.
- The library is built with `-fvisibility=hidden`; only `AOAHID_API` symbols
  are exported.

## Source tree

| Path | Contents |
| --- | --- |
| `include/aoahid.h` | The stable C ABI, with per-function ownership, blocking, synchronization, and result contracts. |
| `include/aoahid.hpp` | Header-only C++ forwarders to the C ABI (namespace `aoahid`). |
| `src/api/` | the ABI boundary in `c_api_context.cpp` (Context and event thread), `c_api_device.cpp` (discovery, accessory start, Devices), `c_api_node.cpp`, `c_api_send.cpp` and `c_api_channel.cpp`; their shared internals in `context_ops.cpp` (deferred errors, option checks, graveyard, HID ID allocation) and `node_ops.cpp` (report state, drain and teardown), declared in `support.hpp`; `error_detail.cpp` (thread-local error record, struct validation). |
| `src/hid/` | Item writer, descriptor builder, descriptor validator, report layout. |
| `src/profiles/` | `spec.cpp` (Spec factories and descriptor generation), `state.cpp` (per-Node report state machines and serialization). |
| `src/transport/` | `discovery.cpp` (runtime, event pumping, request-51 probing), `port.cpp` (shared USB handle, accessory start), `transport.cpp` (requests 54–57, transfer pool), `channel.cpp` (bulk Channels). |
| `bindings/` | Python, C#, and Rust bindings over the C ABI. |
| `tests/` | `unit/`, `sim/` (against the fake libusb in `fake_libusb/`), `hw/`, `abi/`, `golden/` (expected descriptors as hex), `fuzz/`, `cmake/` (package tests), `hidtools/`, `release/`. |
| `tools/` | `symbol-lint/`, `check_*.py` contract and license checks, `generate-support-table/`, `device-check/`, `release/`, `docs/`. |

## Building and checking

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
./build/dev/aoahid_tests profiles   # only the named suites: item_writer, profiles, spec_options, raw_descriptor, transport, identity, api_arguments, report_state, transport_failures
```

Tests require `AOAHID_USE_FAKE_LIBUSB=ON`; the fake backend is never used in
release builds. `-DAOAHID_SANITIZE=ON` enables ASan and UBSan. Library code
compiles with `-Werror -Wconversion -Wsign-conversion -fno-rtti`. CI also runs
clang-format, clang-tidy, the symbol lint, and the `tools/check_*.py` scripts.

## Hardware status

Which devices, hosts, and profiles have been confirmed on real hardware is
recorded in [TARGET_MATRIX.md](TARGET_MATRIX.md); the profiles are described in
[PROFILES.md](PROFILES.md).
