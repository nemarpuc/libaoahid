# C API

`include/aoahid.h` is the stable C ABI. It exposes opaque handles and
fixed-width types only; no libusb or C++ type crosses the boundary. Each
exported function has an Ownership / Blocking / Synchronization / Returns
contract in the header; this page explains how the pieces fit together and
ends with a [reference entry for each function](#function-reference). For
the object model and threading internals see [ARCHITECTURE.md](ARCHITECTURE.md);
for the AOA requests themselves see [PROTOCOL.md](PROTOCOL.md).

Every result and option domain (`aoahid_result`, `aoahid_event_mode`,
`aoahid_profile_kind`, ...) is an `int32_t` typedef with integral constants, so
structure layout does not change when a compiler uses short enums.
`tests/abi/test_c_abi.c` asserts every domain width.

## Structure rules

- Every top-level options structure starts with `struct_size` and `reserved`.
  `struct_size` is `sizeof(struct_type)` and every `reserved*` field is zero.
  A size mismatch fails with `AOAHID_ERR_PARAM`, so an old binary and a new
  header never silently reinterpret a structure.
- Each options structure has an initializer, `aoahid_<structure>_init`, that
  sets `struct_size`, zeroes the rest and fills in values that work with a
  current Android phone (see [Initial values](#initial-values)). Start from it
  and change only what you need.
- A required field left zero is refused with `AOAHID_ERR_UNSET_FIELD`; the
  library never puts a value of its own in its place. Every enumeration starts
  at 1, so a zeroed enumeration field is always refused rather than read as a
  choice. Where zero is a real value (a logical minimum, a neutral axis value,
  interface class 0, a count of optional barrel buttons) the field has no
  "unset" meaning, and what is optional has its own `enable_*` flag or count.
- `enable_*`, `has_*` and the other flags are exactly 0 or 1. A disabled
  field's declaration must be all zero.

## Initial values {#initial-values}

The initializers are `static inline` functions in `aoahid.h`, so they are not
in the binary ABI and the language bindings do not have them; a binding sets
the same fields itself. `aoahid.hpp` adds one function per initializer that
returns the initialized structure (`aoahid::device_options` returns what
`aoahid_device_options_init` fills in; see [C++ header](#c-header)).

| Initializer | What it fills in |
|---|---|
| `aoahid_context_options_init` | `AOAHID_EVENT_INTERNAL_THREAD`, `AOAHID_LOG_DISABLED`. |
| `aoahid_device_options_init` | 4096-byte descriptor fragments, AOA wire limit, EP0 and host control limits; 4 transfer slots; reports up to 1024 bytes; the Linux HID limits of [LIMITS.md](LIMITS.md) (4096, 256, 4, 256, 12288, 65528); `AOAHID_INTERFACE_CLAIM_NONE` with `interface_number = -1`; `validate_reports = 0`. |
| `aoahid_accessory_options_init` | Manufacturer and model `"libaoahid"`; no other string. Set your own when you ship an app that filters accessories. |
| `aoahid_channel_options_init` | The ADB interface `0xFF/0x42/0x01`, `AOAHID_CHANNEL_READ_REQUEST`, 2 OUT transfers of up to 1 MiB, zero-length termination on. |
| `aoahid_node_options_init` | No reserved slot. |
| `aoahid_keyboard_options_init` | Usages `0x04` (Keyboard a) to `0xDD` (Keypad Hexadecimal), plus the modifiers. |
| `aoahid_mouse_options_init` | 5 buttons; X and Y -32767..32767 in 16 bits; wheel and pan -127..127 in 8 bits. |
| `aoahid_toggle_options_init` | Consumer Control (`0x0C/0x01`, field page `0x0C`). The four parallel arrays (`allowed_usages`, `usage_semantics`, `expected_linux_event_types`, `expected_linux_codes`) and `allowed_usage_count` are yours to set. |
| `aoahid_gamepad_options_init` | Axes X, Y, Z, Rz (-127..127 in 8 bits, at rest at 0), 12 buttons from Button 1, an eight-way Hat. |
| `aoahid_touchscreen_options_init` | 10 contacts in one report; Contact ID 0..15 in 4 bits; X and Y 0..32767 in 16 bits; Contact Count 0..10 in 4 bits. |
| `aoahid_touchpad_options_init` | As the touchscreen with 5 contacts, plus one click button. |
| `aoahid_pen_options_init`, `aoahid_digitizer_options_init` | X and Y 0..32767 in 16 bits; pressure 0..4095 in 12 bits; one Barrel Switch; hover. |
| `aoahid_raw_options_init`, `aoahid_capability_manifest_init` | `struct_size` only. |

`aoahid_integer_field_make(minimum, maximum, bits)` builds one field without
physical units.

The timeouts are arguments, not options. `AOAHID_CONTROL_TIMEOUT_MS` (500),
`AOAHID_SEND_TIMEOUT_MS` (500) and `AOAHID_DRAIN_TIMEOUT_MS` (1000) are values
that work with the same phones; the library never uses them by itself.

## Handles and ownership

| Handle | Owns | Lifetime |
|---|---|---|
| `aoahid_context` | One libusb context, optionally one event thread, and a graveyard for Devices whose transfers are still draining. | Destroy last. |
| `aoahid_discovery` | A point-in-time device list and every string/pointer returned by `aoahid_discovery_get`. | Independent of the Context once created. |
| `aoahid_device` | One USB handle, a fixed transfer pool, its Nodes and Channels. | Closed by `aoahid_device_close`, which also closes its children. |
| `aoahid_spec` | An immutable descriptor and report layout. Reference-counted (`aoahid_spec_retain` / `aoahid_spec_release`); usable on any number of Devices and Contexts. | Until the last reference is released. |
| `aoahid_node` | Mutable profile state and one AOA HID registration. Retains its Spec. | Until `aoahid_node_close` returns anything but `AOAHID_CLOSE_PENDING`, or its Device closes. |
| `aoahid_channel` | One claimed Bulk interface on the Device's USB handle, its own transfer pool, and read-ahead state. | Until `aoahid_channel_close` returns anything but `AOAHID_CLOSE_PENDING`, or its Device closes. |

AOA HID IDs are allocated per Context and per physical port (bus plus port
path), starting at 1, and are never reused within that Context, also when the
device enumerates again on the same port. `aoahid_node_id` returns the ID.

`aoahid_spec_descriptor` and `aoahid_spec_manifest` pointers stay valid while
the Spec reference is held. `aoahid_node_manifest` pointers stay valid until the
Node closes.

## Event modes and threading {#event-modes-and-threading}

`aoahid_context_options.event_mode` is one of two modes
(`aoahid_context_options_init` selects the internal thread):

- `AOAHID_EVENT_CALLER_POLL`: the library creates no thread and takes no
  send-path lock. The application serializes every call on the Context and
  drives completions with `aoahid_context_poll(context, timeout_ms)`
  (`timeout_ms = 0` does not wait). Calling `aoahid_context_poll` in the other
  mode returns `AOAHID_ERR_UNSUPPORTED`.
- `AOAHID_EVENT_INTERNAL_THREAD`: the Context runs one event thread. The
  application must still serialize its own calls on one Context; the thread
  only completes transfers. Channel read and write are the exception described
  under [Bulk Channels](#bulk-channels).

The event thread calls libusb's blocking event handler with a 60-second bound;
any USB event wakes it earlier, so this is not a polling interval. Teardown
sets a stop flag and calls `libusb_interrupt_event_handler`, then joins the
thread. If the backend fails immediately, the thread retains the first failure
for an application thread and waits 10 ms before retrying, so it cannot spin.
See the libusb page
[Multi-threaded applications and asynchronous I/O](https://libusb.sourceforge.io/api-1.0/libusb_mtasync.html).

A Node has at most one report in flight. A state call made while it is in
flight returns `AOAHID_ERR_BUSY`, so a completion never consumes a newer
relative delta or lifecycle edge.

`cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan`
runs the test suite under ThreadSanitizer, including repeated overlap of
completion callbacks and Node close. It covers only the concurrency the API
allows; calls the caller must serialize are still the caller's responsibility.

## Functions {#functions}

| Group | Functions |
|---|---|
| Diagnostics | `aoahid_last_error`, `aoahid_result_name`, `aoahid_version` |
| Context | `aoahid_context_create`, `aoahid_context_poll`, `aoahid_context_destroy`, `aoahid_context_destroy_blocking` |
| Discovery | `aoahid_discover`, `aoahid_discovery_count`, `aoahid_discovery_get`, `aoahid_discovery_destroy` |
| Accessory mode | `aoahid_accessory_start` |
| Device | `aoahid_device_open`, `aoahid_device_close`, `aoahid_device_latched_error` |
| Channel | `aoahid_channel_open`, `aoahid_channel_close`, `aoahid_channel_write`, `aoahid_channel_read` |
| Spec factories | `aoahid_spec_create_keyboard`, `_mouse`, `_toggle`, `_gamepad`, `_touchscreen`, `_touchpad`, `_pen`, `_digitizer`, `_raw` |
| Spec access | `aoahid_spec_retain`, `aoahid_spec_release`, `aoahid_spec_descriptor`, `aoahid_spec_manifest` |
| Node | `aoahid_node_open`, `aoahid_node_close`, `aoahid_node_id`, `aoahid_node_manifest`, `aoahid_node_submit`, `aoahid_node_submit_blocking` |
| Profile state | `aoahid_keyboard_key`, `aoahid_mouse_move`, `aoahid_mouse_scroll`, `aoahid_mouse_button`, `aoahid_toggle_usage`, `aoahid_gamepad_button`, `aoahid_gamepad_axis`, `aoahid_gamepad_dpad`, `aoahid_touchscreen_contact`, `aoahid_touchpad_contact`, `aoahid_touchpad_button`, `aoahid_pen_update`, `aoahid_digitizer_update`, `aoahid_battery_update`, `aoahid_raw_submit` |

Each function has an entry in the [Function reference](#function-reference).
`aoahid_version()` returns `(major << 24) | (minor << 16) | patch`; the header
also defines `AOAHID_VERSION_MAJOR`, `_MINOR`, and `_PATCH`.
`aoahid_result_name()` returns the constant's name, or
`"AOAHID_RESULT_UNKNOWN"` for an unknown value.

## Open sequence {#open-sequence}

1. `aoahid_context_create`.
2. `aoahid_discover(context, control_timeout_ms, &discovery)` (a zero timeout
   is `AOAHID_ERR_UNSET_FIELD`). It sends AOA request 51 to every USB device and
   lists only those that answer with a nonzero protocol version, reported in
   `aoahid_device_info.protocol_version`. HID needs version 2 or later; the
   library does not enforce that, so check it. To avoid sending request 51,
   skip discovery and fill `aoahid_device_info` from your own enumeration
   (`protocol_version` stays 0 and is ignored by `aoahid_device_open`).
3. `aoahid_device_open`. It opens the device in its current USB mode, sends no
   AOA request, and never waits for re-enumeration. A device without AOA 2.0
   HID support opens normally and then fails at `aoahid_node_open` with
   `AOAHID_ERR_STALL`.
4. Create Specs with the profile factories (no I/O; may run on any thread).
5. `aoahid_node_open` for each Spec. It allocates Node state and any reserved
   pool slot, then synchronously sends request 54 (register) and the
   descriptor in request-56 fragments. A failure unwinds everything it
   allocated.

   **Wait briefly before the first input.** Android's `f_accessory.c` only
   schedules the HID's registration (`acc_hid_work`) when the last
   descriptor fragment arrives, and refuses request 57 for an ID whose
   `hid_device` that work has not registered yet
   ([PROTOCOL.md](PROTOCOL.md#hid-flow)). Nothing tells the host when it is
   done, so a report sent right after `aoahid_node_open` returns may STALL,
   or be accepted before anything on the phone reads the new input device
   and be lost. Leave a short gap (the README's first program waits 100 ms)
   between opening the Nodes and sending input.
6. Change state with the profile calls and send it with `aoahid_node_submit`
   or `aoahid_node_submit_blocking`.
7. Optionally `aoahid_channel_open` for Bulk data on the same USB handle, for
   example ADB or the accessory interface of an Android app.

### Switching a device to accessory mode

Switching is not required by the library: `aoahid_device_open` works in the
device's current USB mode. The official AOA 2.0 page does not say whether HID
needs `ACCESSORY_START`, and whether a device answers requests 54-57 in its
normal USB mode depends on its kernel gadget configuration
([PROTOCOL.md](PROTOCOL.md#accessory-mode-vs-the-current-usb-mode)), so a
device that STALLs HID registration in its current mode can be switched first. The library exposes
the switch as one call and leaves the rest to the application:

1. `aoahid_accessory_start(context, info, &options, control_timeout_ms)` sends
   request 51, request
   52 for each non-null string in `options.strings` (string IDs 0-5), and
   request 53 (START), then closes the device and returns. `manufacturer` and
   `model` are required and must be non-empty; Android matches them against an
   app's accessory filter. Each string may be at most 256 bytes including its
   NUL, checked before any request is sent (`AOAHID_ERR_OVERFLOW`). A request-51
   response shorter than 2 bytes or reporting version 0 returns
   `AOAHID_ERR_NOT_AOA`. A selection already in accessory mode (VID `0x18D1`,
   PID `0x2D00`-`0x2D05`) receives no request.
2. The device disconnects and re-enumerates as VID `0x18D1`, PID `0x2D00`
   (accessory) or `0x2D01` (accessory + ADB); AOA 2.0 audio adds
   `0x2D02`-`0x2D05`. The library does not wait for this or retry. Rediscover
   on your own schedule and match the same bus and port path. Android cancels
   the switch if the host does not configure the device within about 10
   seconds (`ACCESSORY_REQUEST_TIMEOUT` in AOSP
   `frameworks/base/services/usb/java/com/android/server/usb/UsbDeviceManager.java`).
3. Open the new entry with `aoahid_device_open` and continue at step 4 above.

The library has no call that leaves accessory mode. Unplugging the device, or
`adb shell svc usb setFunctions`, returns it to its normal USB functions. If a
Device is still open on the old enumeration, it reports `AOAHID_ERR_NO_DEVICE`
once the device disconnects.

See [PROTOCOL.md](PROTOCOL.md) for the request details.

## Device options {#device-options}

`aoahid_device_options` has three kinds of fields; `aoahid_device_options_init`
fills in all of them.

**Tuning values**, all required and nonzero (see below):
`descriptor_fragment_bytes`, `transfer_pool_slots`, `maximum_report_bytes`.
The waits (control, send, drain) are not options: every call that can wait
takes its timeout as an argument (see "Timeouts" below).

**Size policies**, all required and nonzero:

| Field | Meaning |
|---|---|
| `aoa_descriptor_wire_policy_bytes` | Largest descriptor to send over AOA (at most 65535, the 16-bit AOA length field). |
| `target_ep0_data_policy_bytes` | Largest control-transfer data stage the target accepts. |
| `host_control_buffer_policy_bytes` | Largest control-transfer data stage the host backend accepts. |

`descriptor_fragment_bytes` and `maximum_report_bytes` must be
at most 65535 and no larger than both the EP0 and host-buffer policies, or open
fails with `AOAHID_ERR_OVERFLOW`. The library never enlarges a policy.

**Target HID parser limits**, all required and nonzero, in
`linux_hid` (`aoahid_linux_hid_policy`): `descriptor_bytes`,
`fields_per_report`, `global_stack_depth`, `report_size_bits`, `usages` and
`report_data_bits`. Every Spec records its own requirements
for these while it is built; `aoahid_node_open` compares them with the Device's
policies and returns `AOAHID_ERR_OVERFLOW` before request 54 if one is
exceeded. Pick values for the kernel you target; [LIMITS.md](LIMITS.md) lists
the values in one Linux revision.

**Other fields**:

- `interface_claim_policy`: `AOAHID_INTERFACE_CLAIM_NONE` with
  `interface_number = -1` (HID on EP0 needs no claimed interface), or
  `AOAHID_INTERFACE_CLAIM_EXPLICIT` with `interface_number` 0-255, which is
  claimed on open and released on close.
- `validate_reports`: 0 or 1. With 1, every generated report is decoded again
  before request 57 and checked for wire length, Report ID prefix, field
  ranges, signedness, Null encoding, and zero padding. With 0 the serializer's
  own bounds checks still run. Raw reports are always checked for an exact
  accepted Report ID and length.

`aoahid_device_open` copies the options; your structure is never modified.

## Timeouts {#timeouts}

No wait is kept in the library: every call that can wait takes its timeout in
milliseconds as an argument, and zero is refused with `AOAHID_ERR_UNSET_FIELD`
before anything is done (nothing is consumed). For libusb a zero timeout
means none at all ("For an unlimited timeout, use value 0",
[`libusb_control_transfer`](https://libusb.sourceforge.io/api-1.0/group__libusb__syncio.html),
retrieved 2026-11-10). A
timeout is a failure deadline, not a delay: a transfer that completes earlier
returns immediately. libmimhid follows the same rule with the same argument
names.

| Argument | Calls | Scope |
|---|---|---|
| `control_timeout_ms` | `aoahid_discover`, `aoahid_accessory_start`, `aoahid_node_open`, `aoahid_node_close`, `aoahid_device_close`, `aoahid_context_destroy`, `aoahid_context_destroy_blocking` | Each synchronous AOA control request (51 to 56). |
| `send_timeout_ms` | `aoahid_node_submit`, `aoahid_raw_submit` | The deadline of the queued asynchronous request-57 transfer; a failure is returned by a later call. A timed-out touch report leaves the contacts it lifted occupied; close and reopen that Node. Keep it well above normal EP0 latency. |
| `drain_timeout_ms` | `aoahid_node_close`, `aoahid_device_close`, `aoahid_context_destroy*`, `aoahid_channel_open`, `aoahid_channel_close` | How long a close (or the wait for in-flight reports before a Channel opens) waits for in-flight reports and for the neutral reports it sends. |
| `timeout_ms` | `aoahid_node_submit_blocking`, `aoahid_channel_read`, `aoahid_channel_write`, `aoahid_context_poll`, `aoahid_context_destroy_blocking` | The whole call. `aoahid_node_submit_blocking` also bounds each report it sends: that report's deadline is what is left of `timeout_ms`; so does the neutral-report wait of a close with `drain_timeout_ms`. |

A Device teardown that cannot finish inside `aoahid_device_close` (a transfer
callback has not run yet) is finished by the Context with the
`control_timeout_ms` of that call.

## Tuning values {#tuning-values}

Zero in a required one fails with `AOAHID_ERR_UNSET_FIELD`; the initializers
fill all of them in. They are the caller's choices, not USB, AOA, Android, ADB,
or libusb requirements.

| Field | Scope |
|---|---|
| `aoahid_device_options.descriptor_fragment_bytes` | Payload of one request-56 fragment. |
| `aoahid_device_options.transfer_pool_slots` | Asynchronous transfers per Device. |
| `aoahid_device_options.maximum_report_bytes` | Largest report per pool slot. |
| `aoahid_channel_options.in_transfers` | IN transfers kept submitted; required in stream read mode, not used in request mode. |
| `aoahid_channel_options.out_transfers` | OUT transfers; a full pool makes a write wait. |
| `aoahid_channel_options.transfer_bytes` | Bytes per Bulk transfer, rounded up to `wMaxPacketSize`; the largest IN request in request mode. |

Zero keeps its plain meaning in `aoahid_node_options`: `has_reserved_slots = 0`
and `reserved_slots = 0` is no reservation (the Node shares the Device pool).
To reserve a slot for a Node, set `has_reserved_slots = 1` and
`reserved_slots = 1` (a Node has one report in flight, so 0 or 1 is allowed),
leaving at least one shared slot on the Device.

## Bulk Channels {#bulk-channels}

`aoahid_channel_open(device, &options, drain_timeout_ms, &channel)`:

1. waits, within `drain_timeout_ms`, until no HID report
   transfer is in flight (`AOAHID_ERR_TIMEOUT` otherwise). When no interface
   is claimed, libusb's WinUSB backend claims one for a control transfer and
   releases it when the transfer completes (`auto_claim` and `auto_release` in
   libusb v1.0.30
   [`libusb/os/windows_winusb.c`](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/windows_winusb.c),
   retrieved 2026-11-10), which would undo a Channel claim made while a
   report was in flight;
2. selects configuration 1 only if the device is unconfigured: on a device
   already in that configuration, `libusb_set_configuration` "will act as a
   lightweight device reset"
   ([libusb device handling](https://libusb.sourceforge.io/api-1.0/group__libusb__dev.html),
   retrieved 2026-11-10);
3. picks the first interface (alternate setting 0) whose class, subclass, and
   protocol equal `interface_class` / `interface_subclass` /
   `interface_protocol` and that has one Bulk IN and one Bulk OUT endpoint
   (`AOAHID_ERR_UNSUPPORTED` if none), claims it on the Device's handle, and
   allocates the Channel's own transfer pool.

HID and Bulk share one handle, so a composite device never needs a second open
(WinUSB refuses one), and Bulk traffic never uses a HID pool slot. Examples:
ADB is `0xFF/0x42/0x01` (`ADB_CLASS` / `ADB_SUBCLASS` / `ADB_PROTOCOL` in AOSP
`packages/modules/adb/adb.h`); the AOA accessory interface of a
`0x2D00`/`0x2D01` device exists only when manufacturer and model were sent.

Opening returns `AOAHID_ERR_BUSY` when another program holds the interface. For
ADB that is usually a running `adb` server; stop it with `adb kill-server`.
HID on EP0 is unaffected.

### Reading

`read_mode` selects how IN transfers are sized:

| `read_mode` | IN transfers | Use when |
|---|---|---|
| `AOAHID_CHANNEL_READ_STREAM` (1) | `in_transfers` transfers of `transfer_bytes` stay submitted. | The device ends every message with a short or zero-length packet, or `transfer_bytes` is one `wMaxPacketSize`. |
| `AOAHID_CHANNEL_READ_REQUEST` (2) | A read with nothing buffered submits one transfer of its `capacity` rounded up to `wMaxPacketSize` (at most `transfer_bytes`). `in_transfers` is ignored. | The protocol states each length up front, as ADB does. |

A Bulk IN transfer completes only when full or on a short packet. In stream
mode, a message whose length is a multiple of `wMaxPacketSize` and is not
followed by a zero-length packet waits until more data arrives. Request mode
reads the way host adb does: it reads the 24-byte `amessage` header, then
exactly `data_length` payload bytes (`UsbReadMessage` and `UsbReadPayload` in
AOSP
[`client/transport_usb.cpp`](https://android.googlesource.com/platform/packages/modules/adb/+/1cf2f017d312f73b3dc53bda85ef2610e35a80e9/client/transport_usb.cpp),
`struct amessage` in
[`types.h`](https://android.googlesource.com/platform/packages/modules/adb/+/1cf2f017d312f73b3dc53bda85ef2610e35a80e9/types.h),
retrieved 2026-11-10), so a read never waits for a zero-length packet.

`aoahid_channel_read` returns bytes in order and may return part of a
transfer; the caller does its own framing. It returns `AOAHID_OK` with at least
one byte, or `AOAHID_ERR_TIMEOUT` if nothing arrived within `timeout_ms`
(0 never waits). In request mode a timed-out transfer stays pending for the
next call, so no byte is lost; bytes beyond a smaller later `capacity` stay
buffered. Zero-length packets carry no data and are skipped.

### Writing

`aoahid_channel_write` copies data into free OUT transfers and returns once
every byte is submitted, without waiting for completion. If the pool stays full
for `timeout_ms` it returns `AOAHID_ERR_TIMEOUT`; `out_written` always reports
how many bytes were queued. A failed OUT completion is reported once by the
next write. Nothing is retried.

`zero_length_termination = 1` ends a write whose length is a nonzero multiple of
`wMaxPacketSize` with an explicit zero-length transfer. libusb's own
`LIBUSB_TRANSFER_ADD_ZERO_PACKET` flag "is currently only supported on Linux"
([libusb asynchronous I/O](https://libusb.sourceforge.io/api-1.0/group__libusb__asyncio.html),
retrieved 2026-11-10), so the library does not use it.

### Errors and threading

A failed IN transfer or a disconnect loses the Channel: reads and writes then
return `AOAHID_ERR_NO_DEVICE`, and the Channel must be closed.

In internal-thread mode one thread may read while another writes, concurrently
with other Context calls, but never concurrently with closing the Channel, its
Device, or the Context. Waiting reads and writes sleep on a condition variable
that the event thread signals. In caller-poll mode read and write belong to the
Context's serialized domain, and a waiting call drives libusb events itself.
Read and write take no lock and do not allocate.

## Submission {#submission}

`aoahid_node_submit(node, send_timeout_ms)` serializes the Node's pending state into a
pool slot and queues request 57 asynchronously, then returns. In caller-poll mode it may run
one non-waiting poll afterwards. If nothing changed it returns `AOAHID_OK`
without sending.

`aoahid_node_submit_blocking(node, timeout_ms)` keeps submitting and pumping
until the Node has nothing left to send or the timeout expires
(`timeout_ms = 0` is `AOAHID_ERR_UNSET_FIELD`). Use it to flush every
mouse-delta fragment, touch continuation packet, and pen tool-change report in
one call. It returns the completion result for its Node.

Failures of asynchronous completions are returned by the next submit on that
Node and by `aoahid_device_latched_error`. `AOAHID_ERR_NO_DEVICE` is sticky for
the Device; other latched errors are reported once.

The library never retries a transfer:

- `AOAHID_ERR_STALL` on request 57 means the device refused the report, so
  nothing was applied. A common cause is a first report sent before Android
  finished registering the HID device (see step 5 above). The refused state
  stays pending; nothing is resent until you call again, after whatever delay
  you choose:
  - `aoahid_node_submit_blocking` returns the STALL itself; the next submit
    call sends the pending state again.
  - `aoahid_node_submit` returns `AOAHID_OK` once the report is queued. The
    STALL is returned by the next submit call, which sends nothing; the call
    after that sends the pending state again.
  - A Raw Node keeps no copy of its bytes: resend by calling
    `aoahid_raw_submit` again. Until one succeeds, `aoahid_node_submit_blocking`
    on it returns `AOAHID_ERR_UNSUPPORTED`.

  `examples/c/verify/verify_common.h` shows a bounded resend.
- Timeout, cancellation, short transfer, and I/O errors consume the submitted
  state, because the report may have been delivered and resending could repeat
  a key press, contact, or relative motion.

## Profile state calls

Each profile has one set of state calls. Every call validates its arguments
against the Spec, returns `AOAHID_ERR_BUSY` while a report is in flight, and
returns `AOAHID_ERR_NO_DEVICE` once the Device is lost. None of them performs
I/O or allocates.

| Profile | Calls |
|---|---|
| Keyboard | `aoahid_keyboard_key(node, usage, down)` |
| Mouse | `aoahid_mouse_move(node, dx, dy)`, `aoahid_mouse_scroll(node, wheel, pan)`, `aoahid_mouse_button(node, button, down)` |
| Toggle | `aoahid_toggle_usage(node, usage, down)` |
| Gamepad | `aoahid_gamepad_button(node, button, down)`, `aoahid_gamepad_axis(node, axis_index, value)`, `aoahid_gamepad_dpad(node, up, down, right, left)` |
| Touchscreen | `aoahid_touchscreen_contact(node, contact_id, down, x, y, extra)` |
| Touchpad | `aoahid_touchpad_contact(node, contact_id, down, x, y, extra)`, `aoahid_touchpad_button(node, button, down)` |
| Pen | `aoahid_pen_update(node, &sample)` |
| Digitizer | `aoahid_digitizer_update(node, &sample)` |
| Battery report (any profile with `enable_battery`) | `aoahid_battery_update(node, strength)` |
| Raw | `aoahid_raw_submit(node, report, length, send_timeout_ms)` (validates, copies, and queues request 57 directly) |

`down` and the D-pad directions must be exactly 0 or 1. Buttons are
one-based. What each call derives, and which values stay the caller's choice,
is in [PROFILES.md](PROFILES.md). In short:

- **Edge guard.** A press/release edge that has not yet been accepted in a
  report cannot be replaced by the opposite edge; that call returns
  `AOAHID_ERR_BUSY` until the pending edge is sent. This applies to keys,
  buttons, toggles, D-pad, touch contacts, and pen tool state.
- **Keyboard.** `usage` is a modifier (`0xE0`-`0xE7`) or a Usage in the Spec's
  range. There is no release-all call; closing the Node sends a report with
  every key up.
- **Mouse.** Deltas accumulate in signed 64-bit totals (overflow returns
  `AOAHID_ERR_OVERFLOW`); `aoahid_mouse_scroll` on an axis the Spec did not
  enable returns `AOAHID_ERR_UNSUPPORTED`.
- **Toggle.** `down = 1` presses an allow-listed `usage` and `down = 0` releases
  that same `usage`. Each Usage is its own bit, so several can be held at once.
- **Touch.** `down = 1` places a new `contact_id` or moves an active one;
  `down = 0` lifts an active one at the given final x/y. `extra` may be null;
  a nonzero value for a field the Spec did not enable is rejected. No free
  slot returns `AOAHID_ERR_OVERFLOW`.
- **Pen and Digitizer.** Each call sends the whole state as one sample. There is
  no separate depart call: `in_range = 0` takes the tool away, and X/Y, tilt,
  and twist in that sample go out as given, so repeat the last position to
  leave the pointer where it was.
- **Touch together with a mouse.** A Touchscreen Node and a Mouse Node on the
  same device are separate input devices; the library does not coordinate
  their reports.
- **Battery report.** `aoahid_battery_update` works on a Node whose Spec was
  created with `enable_battery = 1`. The level is sent by the next submit that
  has no input report to send, and only when it changed.

No profile generates key repeat, debounce, long-press, or any other timed
action.

### C++ header {#c-header}

`include/aoahid.hpp` forwards every function of `aoahid.h` unchanged under
namespace `aoahid`, without the `aoahid_` prefix (`aoahid_keyboard_key` is
`aoahid::keyboard_key`). Types, arguments, and results are those of the C API;
the header adds no type aliases. It adds two things:

- one function per initializer that returns the initialized structure by
  value: `aoahid::context_options`, `aoahid::device_options`,
  `aoahid::accessory_options`, `aoahid::channel_options`,
  `aoahid::node_options`, one per profile (`aoahid::keyboard_options` to
  `aoahid::raw_options`), and `aoahid::capability_manifest`;
- `aoahid::SpecPtr` and `aoahid::DiscoveryPtr`, `std::unique_ptr` types that
  release a Spec reference or destroy a discovery snapshot. Context, Device,
  Node and Channel stay plain pointers: closing them takes timeouts and may
  return `AOAHID_CLOSE_PENDING`, which a destructor could neither report nor
  retry.

## Manifest

`aoahid_spec_manifest` and `aoahid_node_manifest` fill an
`aoahid_capability_manifest` (set `struct_size`, zero `reserved`):

- `input_supported = 1`, `output_supported = 0`,
  `feature_transport_supported = 0`;
- `android_status`: `AOAHID_ANDROID_PORTABLE_CANDIDATE` for a form that follows
  documented Android input behavior, `AOAHID_ANDROID_CONDITIONAL` when behavior
  depends on the Android release, kernel, or OEM, and `AOAHID_ANDROID_UNKNOWN`
  for Raw Specs. This is a static classification of the descriptor, not a test
  result; see [PROFILES.md](PROFILES.md#hardware-status) for what has been
  tested;
- `profile_kind`;
- `reports` / `report_count`: every Input Report ID and its exact wire length;
- `descriptor_length`.

A generated descriptor may contain a Constant Feature item such as Contact
Count Maximum. It is metadata only; `feature_transport_supported` stays 0 and
there is no Feature or Output path back to the host.

## Close and destroy {#close-and-destroy}

Closing a Node waits for its in-flight report, sends a neutral/release report
if the device is still present, then sends request 55 (unregister). No
transfer, buffer, handle, or object is freed before its libusb callback has
run.

Every close and destroy call follows one rule:

| Result | The handle |
|---|---|
| `AOAHID_ERR_PARAM`, `AOAHID_ERR_UNSET_FIELD` | Nothing was done; it is still yours. |
| `AOAHID_CLOSE_PENDING` | A transfer of the object is still in flight; it is still yours. Let events run (`aoahid_context_poll` in caller-poll mode) and call again. |
| Anything else | The object is gone. An error says what failed on the way out, such as a report that failed earlier or a refused unregister (request 55). |

Where `AOAHID_CLOSE_PENDING` can come from:

- `aoahid_node_close`: a report of the Node is still in flight at the drain
  deadline.
- `aoahid_channel_close`: a cancelled Bulk transfer has not completed.
- `aoahid_context_destroy`: a Device teardown is still waiting for a callback.
- `aoahid_context_destroy_blocking`: as `aoahid_context_destroy`, when its own
  `timeout_ms` passed first.

`aoahid_device_close` never returns it. When a transfer of the Device is still
in flight at the drain deadline, the Context takes the teardown over, frees
everything once the callbacks have run, and reports any error of it from
`aoahid_context_poll` or `aoahid_context_destroy`. A consumed pointer must never
be passed to the API again.

## Errors {#errors}

`aoahid_last_error()` returns the calling thread's diagnostic record without
changing it. Every other function replaces the record: success resets it to
`AOAHID_OK`; a failure fills `code`, `field`, `reason`, and, where they apply,
`transport_status` (the libusb status), `aoa_request`, `node_id` (the AOA HID ID of the Node involved), `report_id`, `offset`, and `length`.
The strings stay valid until the thread's next call into the library.
Functions that return a value rather than a result (`aoahid_discovery_count`,
`aoahid_discovery_get`, `aoahid_node_id`, `aoahid_version`) use 0 or null as
the failure value, and `void` functions report failure only through
`aoahid_last_error()`.

Output handles (`out_context`, `out_discovery`, `out_device`, `out_spec`,
`out_node`, `out_channel`) are null on every failure.

The event thread never writes to an application thread's record. Completion
errors are stored on the Node, Device, or Context and published on the next
application call that touches them. If the event thread itself stops, its
error is returned by every later call on that Context. A submit that libusb
accepted stays successful even if the poll that follows it fails; that poll
error is returned by the next call.

| Code | Meaning |
|---|---|
| `AOAHID_ERR_PARAM` | Invalid pointer, handle, struct size, or argument. |
| `AOAHID_ERR_UNSET_FIELD` | A required field or timeout is zero, or a field has no valid value. |
| `AOAHID_ERR_UNSUPPORTED` | Not supported by this profile, mode, or backend. |
| `AOAHID_ERR_NOT_AOA` | The device does not report AOA support (`aoahid_accessory_start`). |
| `AOAHID_ERR_ACCESS`, `_BUSY`, `_NO_DEVICE`, `_TIMEOUT`, `_IO` | Mapped libusb status. `AOAHID_ERR_BUSY` also means "report in flight" for state calls. `AOAHID_ERR_NO_DEVICE` is sticky for the Device. |
| `AOAHID_ERR_STALL` | A control request stalled. This alone does not prove the descriptor was rejected. |
| `AOAHID_ERR_SHORT_TRANSFER` | A control transfer moved fewer bytes than its payload (the 8-byte setup packet is not counted). |
| `AOAHID_ERR_DESCRIPTOR_REJECTED` | Reserved result code; `aoahid_result_name` knows it. |
| `AOAHID_ERR_OVERFLOW` | A size, count, or policy limit was exceeded, or libusb reported `LIBUSB_TRANSFER_OVERFLOW` (the device sent more data than requested). |
| `AOAHID_CLOSE_PENDING` | A close could not finish within its budget; the handle is still the caller's. |
| `AOAHID_ERR_INTERNAL` | Allocation failure or unexpected internal error. |

No C++ exception crosses the C ABI. Allocation failures and unexpected
exceptions are caught at the exported function, returned as
`AOAHID_ERR_INTERNAL`, and partly built objects release their USB and HID
resources first.

## Logging

`aoahid_context_options.log_level` is `AOAHID_LOG_DISABLED`, `_ERROR`, `_INFO`,
or `_TRACE`. Any level other than disabled requires a non-null `log_sink`
(`AOAHID_ERR_UNSET_FIELD` otherwise). Only cold paths (open, close,
registration, errors) log; submission and completion never do. Records include
bus/port path, VID/PID, HID and Report IDs, request, offset, length, result,
and libusb status. Report payload bytes are never logged.

The sink is called synchronously on the thread that produced the record,
including the internal event thread. It must return promptly, must not call
back into or destroy the same Context, and must be thread-safe in
internal-thread mode. The library holds no internal lock while calling it.

## Function reference {#function-reference}

One entry per exported function, in the order of `include/aoahid.h`. The
signature, Ownership, Blocking, Threads (the header's "Synchronization") and
Returns lines are the header's contract; `tools/check_public_contracts.py`
checks that the header has all four for every function. Each entry lists only
what is particular to the function: the rules every function follows
(results recorded for `aoahid_last_error`, `out_*` pointers set to null first,
the close rule, zero timeouts refused, `AOAHID_ERR_INTERNAL` from any call)
are in [Errors](#errors), [Close and destroy](#close-and-destroy) and
[Timeouts](#timeouts). "Context domain" means the call must not run at the same
time as any other call on the same Context, its Devices, Nodes or Channels
([Event modes and threading](#event-modes-and-threading)).

### Diagnostics

#### aoahid_last_error

```c
const aoahid_error_detail* aoahid_last_error(void);
```

**Ownership:** Returns the calling thread's record, valid until that thread's next call.

**Blocking:** None.

**Threads:** The record belongs to the calling thread; do not pass it to another.

**Returns:** Never null. Results of report transfers completed on the event thread are not here; they come back from a later call (see [Errors](#errors)), `AOAHID_OK` and `AOAHID_ERR_*` alike.

#### aoahid_result_name

```c
const char* aoahid_result_name(aoahid_result result);
```

- `result`: any value.

**Ownership:** Returns a string that lives as long as the process.

**Blocking:** None.

**Threads:** Any thread.

**Returns:** The constant's name, such as `"AOAHID_OK"` or `"AOAHID_ERR_STALL"`, or `"AOAHID_RESULT_UNKNOWN"` (with `AOAHID_ERR_PARAM` recorded) for a value that is not a result.

#### aoahid_version

```c
uint32_t aoahid_version(void);
```

**Ownership:** Nothing.

**Blocking:** None.

**Threads:** Any thread.

**Returns:** `(major << 24) | (minor << 16) | patch` of the loaded library; `AOAHID_OK` and the `AOAHID_ERR_*` codes do not apply.

### Context

#### aoahid_context_create

```c
aoahid_result aoahid_context_create(const aoahid_context_options* options, aoahid_context** out_context);
```

- `options`: the options; start from `aoahid_context_options_init`.
- `out_context`: receives the new Context; set to null first and left null when the call fails.

**Ownership:** Keeps log_sink and log_user until the Context is destroyed.

**Blocking:** Initializes libusb and, in `AOAHID_EVENT_INTERNAL_THREAD` mode, starts the event thread. Sends nothing to any device.

**Threads:** Any thread; the new Context is a domain of its own.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for a null pointer or a wrong struct_size; `AOAHID_ERR_UNSET_FIELD` for an unknown event mode or log level, or a log level without a sink; a transport error when libusb cannot start.

#### aoahid_context_poll

```c
aoahid_result aoahid_context_poll(aoahid_context* context, uint32_t timeout_ms);
```

- `context`: the Context.
- `timeout_ms`: how long to handle events, in milliseconds; zero handles only what is ready.

**Ownership:** Nothing.

**Blocking:** Handles libusb events for up to timeout_ms (zero only handles what is ready). Report completions and Device teardowns the Context finishes run inside this call.

**Threads:** Context domain; `AOAHID_EVENT_CALLER_POLL` only.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for a null Context; `AOAHID_ERR_UNSUPPORTED` in `AOAHID_EVENT_INTERNAL_THREAD` mode; a transport error from libusb or an earlier failure the Context kept, such as one of a Device teardown it finished.

#### aoahid_context_destroy

```c
aoahid_result aoahid_context_destroy(aoahid_context* context, uint32_t control_timeout_ms, uint32_t drain_timeout_ms);
```

- `context`: the Context.
- `control_timeout_ms`: the deadline of each AOA control request, in milliseconds; zero is `AOAHID_ERR_UNSET_FIELD`.
- `drain_timeout_ms`: how long to wait for transfers in flight, in milliseconds (see Blocking); zero is `AOAHID_ERR_UNSET_FIELD`.

**Ownership:** Closes every open Device as `aoahid_device_close` does, then frees the Context. Follows the [close rule](#close-and-destroy): `AOAHID_CLOSE_PENDING` while a Device teardown is still waiting for a transfer callback.

**Blocking:** control_timeout_ms for each AOA request and drain_timeout_ms for each Device's reports, then joins the event thread. It never waits for a callback past these.

**Threads:** Context domain; not from the log sink.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for a null Context; `AOAHID_ERR_UNSET_FIELD` for a zero timeout; `AOAHID_CLOSE_PENDING`; otherwise the first error of the teardown, and the Context is gone.

#### aoahid_context_destroy_blocking

```c
aoahid_result aoahid_context_destroy_blocking(aoahid_context* context, uint32_t control_timeout_ms, uint32_t drain_timeout_ms, uint32_t timeout_ms);
```

- `context`: the Context.
- `control_timeout_ms`: the deadline of each AOA control request, in milliseconds; zero is `AOAHID_ERR_UNSET_FIELD`.
- `drain_timeout_ms`: how long to wait for transfers in flight, in milliseconds (see Blocking); zero is `AOAHID_ERR_UNSET_FIELD`.
- `timeout_ms`: the deadline of the whole call, in milliseconds; zero is `AOAHID_ERR_UNSET_FIELD`.

**Ownership:** As `aoahid_context_destroy`, retried until it no longer returns `AOAHID_CLOSE_PENDING` or timeout_ms has passed.

**Blocking:** Up to timeout_ms, handling libusb events itself when no thread does.

**Threads:** Context domain; not from the event thread or the log sink.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for a null Context; `AOAHID_ERR_UNSET_FIELD` for a zero timeout; `AOAHID_CLOSE_PENDING` when timeout_ms passed first (the Context is still the caller's); otherwise as `aoahid_context_destroy`.

### Discovery and accessory mode

#### aoahid_discover

```c
aoahid_result aoahid_discover(aoahid_context* context, uint32_t control_timeout_ms, aoahid_discovery** out_discovery);
```

The snapshot is outside it.

- `context`: the Context.
- `control_timeout_ms`: the deadline of each AOA control request, in milliseconds; zero is `AOAHID_ERR_UNSET_FIELD`.
- `out_discovery`: receives the new snapshot; set to null first and left null when the call fails.

**Ownership:** Returns a snapshot that owns every entry and string in it; free it with `aoahid_discovery_destroy`.

**Blocking:** Sends AOA request 51 to every USB device, waiting up to control_timeout_ms for each.

**Threads:** Context domain.

**Returns:** `AOAHID_OK`, with only the devices that answered a nonzero AOA version (HID needs 2); `AOAHID_ERR_PARAM` for a null argument; `AOAHID_ERR_UNSET_FIELD` for a zero timeout; a transport error from enumeration.

#### aoahid_discovery_count

```c
size_t aoahid_discovery_count(const aoahid_discovery* discovery);
```

- `discovery`: the snapshot.

**Ownership:** Nothing.

**Blocking:** None.

**Threads:** Any thread, but not during `aoahid_discovery_destroy`.

**Returns:** The number of entries. Zero is also returned for a null snapshot, with `AOAHID_ERR_PARAM` recorded; `AOAHID_OK` is recorded otherwise.

#### aoahid_discovery_get

```c
const aoahid_device_info* aoahid_discovery_get(const aoahid_discovery* discovery, size_t index);
```

- `discovery`: the snapshot.
- `index`: the entry, from 0 to `aoahid_discovery_count` minus 1.

**Ownership:** The entry and its strings belong to the snapshot.

**Blocking:** None.

**Threads:** Any thread, but not during `aoahid_discovery_destroy`.

**Returns:** The entry with `AOAHID_OK` recorded, or null with `AOAHID_ERR_PARAM` recorded for a null snapshot or an index out of range.

#### aoahid_discovery_destroy

```c
void aoahid_discovery_destroy(aoahid_discovery* discovery);
```

- `discovery`: the snapshot.

**Ownership:** Frees the snapshot and every entry it returned.

**Blocking:** None.

**Threads:** Not during any other call on the snapshot.

**Returns:** Nothing. `AOAHID_OK` is recorded, or `AOAHID_ERR_PARAM` for null (nothing is done).

#### aoahid_accessory_start

```c
aoahid_result aoahid_accessory_start(aoahid_context* context, const aoahid_device_info* selected, const aoahid_accessory_options* options, uint32_t control_timeout_ms);
```

- `context`: the Context.
- `selected`: the device: an entry of `aoahid_discovery_get`, or one the caller filled in (only `bus_number`, `device_address` and `port_path` identify it).
- `options`: the options; start from `aoahid_accessory_options_init`.
- `control_timeout_ms`: the deadline of each AOA control request, in milliseconds; zero is `AOAHID_ERR_UNSET_FIELD`.

**Ownership:** Nothing is kept.

**Blocking:** Sends AOA request 51, request 52 for each non-null string and request 53 (start), each waiting up to control_timeout_ms, and returns once request 53 completes. The device then disconnects and comes back in accessory mode (VID 0x18D1, PID 0x2D00 to 0x2D05); find it again with `aoahid_discover`. A device already in accessory mode gets no request.

**Threads:** Context domain.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for a null argument or a wrong struct_size; `AOAHID_ERR_UNSET_FIELD` for a zero timeout, a missing manufacturer or model, or a nonzero reserved; `AOAHID_ERR_NOT_AOA` when request 51 shows no AOA support; `AOAHID_ERR_OVERFLOW`, before any request, for a string over 256 bytes or a port path over 7 ports; a transport error from the requests (after a failed request 53 the device may still have switched).

### Device

#### aoahid_device_open

```c
aoahid_result aoahid_device_open(aoahid_context* context, const aoahid_device_info* selected, const aoahid_device_options* options, aoahid_device** out_device);
```

- `context`: the Context.
- `selected`: the device: an entry of `aoahid_discovery_get`, or one the caller filled in (only `bus_number`, `device_address` and `port_path` identify it).
- `options`: the options; start from `aoahid_device_options_init`.
- `out_device`: receives the new Device; set to null first and left null when the call fails.

**Ownership:** Copies selected and options. The Device belongs to the caller and to the Context, which closes it if it is still open when the Context is destroyed.

**Blocking:** Opens the USB device as it is, claims interface_number if asked, and allocates the transfer pool. Sends no AOA request: a device without AOA 2.0 HID opens and then fails at `aoahid_node_open` with `AOAHID_ERR_STALL`.

**Threads:** Context domain.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for a null argument, a wrong struct_size or an invalid value; `AOAHID_ERR_UNSET_FIELD` for a zero required field; `AOAHID_ERR_OVERFLOW` for a size above one of the byte policies or a port path over 7 ports; a transport error from opening or claiming.

#### aoahid_device_close

```c
aoahid_result aoahid_device_close(aoahid_device* device, uint32_t control_timeout_ms, uint32_t drain_timeout_ms);
```

- `device`: the Device.
- `control_timeout_ms`: the deadline of each AOA control request, in milliseconds; zero is `AOAHID_ERR_UNSET_FIELD`.
- `drain_timeout_ms`: how long to wait for transfers in flight, in milliseconds (see Blocking); zero is `AOAHID_ERR_UNSET_FIELD`.

**Ownership:** Closes every Node (as `aoahid_node_close` does) and Channel of the Device, then the Device. It never returns `AOAHID_CLOSE_PENDING`: when a transfer is still in flight at the drain deadline, the Context finishes the teardown and reports its errors from `aoahid_context_poll` or `aoahid_context_destroy`.

**Blocking:** drain_timeout_ms for all reports together, control_timeout_ms for each request 55.

**Threads:** Context domain; not from the log sink.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for a null Device or one already closed; `AOAHID_ERR_UNSET_FIELD` for a zero timeout; otherwise the first error of the teardown.

#### aoahid_device_latched_error

```c
aoahid_result aoahid_device_latched_error(aoahid_device* device);
```

The event thread may record an error at the same time.

- `device`: the Device.

**Ownership:** Clears the error it returns, except `AOAHID_ERR_NO_DEVICE`, which stays.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** `AOAHID_OK` when no error is waiting; `AOAHID_ERR_PARAM` for an invalid Device; otherwise the error a report transfer, control request or event left on the Device or its Context.

### Channel

#### aoahid_channel_open

```c
aoahid_result aoahid_channel_open(aoahid_device* device, const aoahid_channel_options* options, uint32_t drain_timeout_ms, aoahid_channel** out_channel);
```

- `device`: the Device.
- `options`: the options; start from `aoahid_channel_options_init`.
- `drain_timeout_ms`: how long to wait for transfers in flight, in milliseconds (see Blocking); zero is `AOAHID_ERR_UNSET_FIELD`.
- `out_channel`: receives the new Channel; set to null first and left null when the call fails.

**Ownership:** The Channel shares the Device's USB handle; `aoahid_device_close` closes it too.

**Blocking:** Waits up to drain_timeout_ms for report transfers in flight, selects configuration 1 if the device has none (AOA 1.0), claims the interface, and submits the read-ahead transfers.

**Threads:** Context domain.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for a null argument, a wrong struct_size or a closing Device; `AOAHID_ERR_UNSET_FIELD` for a zero required field or timeout, a nonzero reserved field, or a flag other than 0 or 1; `AOAHID_ERR_UNSUPPORTED` when no interface matches; `AOAHID_ERR_TIMEOUT` when report transfers were still in flight; `AOAHID_ERR_OVERFLOW` for transfer_bytes above the libusb limit; a transport error from claiming or submitting.

#### aoahid_channel_close

```c
aoahid_result aoahid_channel_close(aoahid_channel* channel, uint32_t drain_timeout_ms);
```

- `channel`: the Channel.
- `drain_timeout_ms`: how long to wait for transfers in flight, in milliseconds (see Blocking); zero is `AOAHID_ERR_UNSET_FIELD`.

**Ownership:** Follows the [close rule](#close-and-destroy): `AOAHID_CLOSE_PENDING` while a cancelled transfer has not completed.

**Blocking:** Cancels every transfer and waits up to drain_timeout_ms for them, then within the same time for report transfers in flight, and releases the interface.

**Threads:** Context domain; never during a read or write on the Channel.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for a null Channel; `AOAHID_ERR_UNSET_FIELD` for a zero timeout; `AOAHID_CLOSE_PENDING`; a transport error from handling libusb events.

#### aoahid_channel_write

```c
aoahid_result aoahid_channel_write(aoahid_channel* channel, const uint8_t* data, size_t length, size_t* out_written, uint32_t timeout_ms);
```

- `channel`: the Channel.
- `data`: the bytes to write.
- `length`: how many.
- `out_written`: receives how many bytes were queued.
- `timeout_ms`: how long to wait for free OUT transfers, in milliseconds; zero does not wait.

**Ownership:** Copies data. out_written is set on every return.

**Blocking:** Waits up to timeout_ms (zero does not wait) for free OUT transfers and returns once every byte is submitted. It does not wait for completion and never retries.

**Threads:** In `AOAHID_EVENT_INTERNAL_THREAD` mode one thread may write while another reads, alongside other calls on the Context, but not while the Channel, its Device or the Context is closing. In `AOAHID_EVENT_CALLER_POLL` mode, Context domain.

**Returns:** `AOAHID_OK` when every byte was queued; `AOAHID_ERR_PARAM` for a null pointer; `AOAHID_ERR_TIMEOUT` when the transfers stayed busy; `AOAHID_ERR_NO_DEVICE` once the Channel is lost; a transport error of an earlier write, returned once.

#### aoahid_channel_read

```c
aoahid_result aoahid_channel_read(aoahid_channel* channel, uint8_t* buffer, size_t capacity, size_t* out_received, uint32_t timeout_ms);
```

- `channel`: the Channel.
- `buffer`: where the bytes go.
- `capacity`: its size in bytes; zero is `AOAHID_ERR_PARAM`.
- `out_received`: receives how many bytes were read.
- `timeout_ms`: how long to wait for data, in milliseconds; zero does not wait.

**Ownership:** out_received is set on every return.

**Blocking:** Waits up to timeout_ms (zero does not wait) for data. A call may return part of a transfer or stop at a transfer boundary. In `AOAHID_CHANNEL_READ_REQUEST` mode a read with nothing buffered submits one transfer sized from capacity; a timeout leaves it submitted and the next read waits for it, so nothing is lost. Zero-length packets are skipped.

**Threads:** As `aoahid_channel_write`; one reader at a time.

**Returns:** `AOAHID_OK` with at least one byte; `AOAHID_ERR_PARAM` for a null pointer or a zero capacity; `AOAHID_ERR_TIMEOUT` when nothing arrived; `AOAHID_ERR_NO_DEVICE` once the Channel is lost; a transport error of a failed IN transfer, which also loses the Channel.

### Spec

The factories copy their options and every array and string the options
point to. The new Spec holds one reference for the caller. "As every factory"
below means: `AOAHID_OK`; `AOAHID_ERR_PARAM` for a null pointer, a wrong
`struct_size` or a value out of range; `AOAHID_ERR_UNSET_FIELD` for a required
field left zero; `AOAHID_ERR_OVERFLOW` when the descriptor or report would be
too large. What each field may hold is in [PROFILES.md](PROFILES.md).

#### aoahid_spec_create_keyboard

```c
aoahid_result aoahid_spec_create_keyboard(const aoahid_keyboard_options* options, aoahid_spec** out_spec);
```

- `options`: the options; start from `aoahid_keyboard_options_init`.
- `out_spec`: receives the new Spec; set to null first and left null when the call fails.

**Ownership:** As every factory.

**Blocking:** None.

**Threads:** Any thread.

**Returns:** As every factory, `AOAHID_OK` through `AOAHID_ERR_OVERFLOW`; `AOAHID_ERR_PARAM` also for a range that includes 0x00 to 0x03 or the modifiers.

#### aoahid_spec_create_mouse

```c
aoahid_result aoahid_spec_create_mouse(const aoahid_mouse_options* options, aoahid_spec** out_spec);
```

- `options`: the options; start from `aoahid_mouse_options_init`.
- `out_spec`: receives the new Spec; set to null first and left null when the call fails.

**Ownership:** As every factory.

**Blocking:** None.

**Threads:** Any thread.

**Returns:** As every factory, `AOAHID_OK` through `AOAHID_ERR_OVERFLOW`.

#### aoahid_spec_create_toggle

```c
aoahid_result aoahid_spec_create_toggle(const aoahid_toggle_options* options, aoahid_spec** out_spec);
```

- `options`: the options; start from `aoahid_toggle_options_init`.
- `out_spec`: receives the new Spec; set to null first and left null when the call fails.

**Ownership:** As every factory.

**Blocking:** None.

**Threads:** Any thread.

**Returns:** As every factory, `AOAHID_OK` through `AOAHID_ERR_OVERFLOW`; `AOAHID_ERR_UNSUPPORTED` for a semantic that press and release cannot express, or a Camera Control Usage that is not `AOAHID_USAGE_ONE_SHOT`.

#### aoahid_spec_create_gamepad

```c
aoahid_result aoahid_spec_create_gamepad(const aoahid_gamepad_options* options, aoahid_spec** out_spec);
```

- `options`: the options; start from `aoahid_gamepad_options_init`.
- `out_spec`: receives the new Spec; set to null first and left null when the call fails.

**Ownership:** As every factory.

**Blocking:** None.

**Threads:** Any thread.

**Returns:** As every factory, `AOAHID_OK` through `AOAHID_ERR_OVERFLOW`.

#### aoahid_spec_create_touchscreen

```c
aoahid_result aoahid_spec_create_touchscreen(const aoahid_touchscreen_options* options, aoahid_spec** out_spec);
```

- `options`: the options; start from `aoahid_touchscreen_options_init`.
- `out_spec`: receives the new Spec; set to null first and left null when the call fails.

**Ownership:** As every factory.

**Blocking:** None.

**Threads:** Any thread.

**Returns:** As every factory, `AOAHID_OK` through `AOAHID_ERR_OVERFLOW`.

#### aoahid_spec_create_touchpad

```c
aoahid_result aoahid_spec_create_touchpad(const aoahid_touchpad_options* options, aoahid_spec** out_spec);
```

- `options`: the options; start from `aoahid_touchpad_options_init`.
- `out_spec`: receives the new Spec; set to null first and left null when the call fails.

**Ownership:** As every factory.

**Blocking:** None.

**Threads:** Any thread.

**Returns:** As every factory, `AOAHID_OK` through `AOAHID_ERR_OVERFLOW`.

#### aoahid_spec_create_pen

```c
aoahid_result aoahid_spec_create_pen(const aoahid_pen_options* options, aoahid_spec** out_spec);
```

- `options`: the options; start from `aoahid_pen_options_init`.
- `out_spec`: receives the new Spec; set to null first and left null when the call fails.

**Ownership:** As every factory.

**Blocking:** None.

**Threads:** Any thread.

**Returns:** As every factory, `AOAHID_OK` through `AOAHID_ERR_OVERFLOW`.

#### aoahid_spec_create_digitizer

```c
aoahid_result aoahid_spec_create_digitizer(const aoahid_digitizer_options* options, aoahid_spec** out_spec);
```

- `options`: the options; start from `aoahid_digitizer_options_init`.
- `out_spec`: receives the new Spec; set to null first and left null when the call fails.

**Ownership:** As every factory.

**Blocking:** None.

**Threads:** Any thread.

**Returns:** As every factory, `AOAHID_OK` through `AOAHID_ERR_OVERFLOW`.

#### aoahid_spec_create_raw

```c
aoahid_result aoahid_spec_create_raw(const aoahid_raw_options* options, aoahid_spec** out_spec);
```

- `options`: the options; start from `aoahid_raw_options_init`.
- `out_spec`: receives the new Spec; set to null first and left null when the call fails.

**Ownership:** As every factory.

**Blocking:** None.

**Threads:** Any thread.

**Returns:** As every factory, `AOAHID_OK` through `AOAHID_ERR_OVERFLOW`; `AOAHID_ERR_PARAM` also for a descriptor that does not parse or does not match the report table; `AOAHID_ERR_UNSUPPORTED` without the acknowledgement, with requires_output or requires_feature_response set, or for a long item or a construct the parser does not handle.

#### aoahid_spec_retain

```c
void aoahid_spec_retain(aoahid_spec* spec);
```

- `spec`: the Spec.

**Ownership:** Adds a reference for the caller, who must already hold one.

**Blocking:** None.

**Threads:** Any thread.

**Returns:** Nothing. `AOAHID_OK` is recorded; `AOAHID_ERR_PARAM` for null, or `AOAHID_ERR_OVERFLOW` when the count is full (no reference is added).

#### aoahid_spec_release

```c
void aoahid_spec_release(aoahid_spec* spec);
```

- `spec`: the Spec.

**Ownership:** Gives up one of the caller's references; the last one frees the Spec. A Node keeps its own reference until it closes.

**Blocking:** None.

**Threads:** Any thread, as long as the last release does not race any other use.

**Returns:** Nothing. `AOAHID_OK` is recorded, or `AOAHID_ERR_PARAM` for null (nothing is done).

#### aoahid_spec_descriptor

```c
aoahid_result aoahid_spec_descriptor(const aoahid_spec* spec, const uint8_t** bytes, size_t* length);
```

- `spec`: the Spec.
- `bytes`: receives a pointer to the descriptor, which belongs to the Spec.
- `length`: receives its length in bytes.

**Ownership:** The bytes belong to the Spec.

**Blocking:** None.

**Threads:** Any thread.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for a null pointer.

#### aoahid_spec_manifest

```c
aoahid_result aoahid_spec_manifest(const aoahid_spec* spec, aoahid_capability_manifest* manifest);
```

- `spec`: the Spec.
- `manifest`: the caller's manifest, with `struct_size` set (`aoahid_capability_manifest_init`) and `reserved` zero.

**Ownership:** Fills the caller's manifest; manifest->reports belongs to the Spec.

**Blocking:** None.

**Threads:** Any thread.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for a null pointer, a wrong struct_size or a nonzero reserved.

### Node

#### aoahid_node_open

```c
aoahid_result aoahid_node_open(aoahid_device* device, aoahid_spec* spec, const aoahid_node_options* options, uint32_t control_timeout_ms, aoahid_node** out_node);
```

- `device`: the Device.
- `spec`: the Spec.
- `options`: the options; start from `aoahid_node_options_init`.
- `control_timeout_ms`: the deadline of each AOA control request, in milliseconds; zero is `AOAHID_ERR_UNSET_FIELD`.
- `out_node`: receives the new Node; set to null first and left null when the call fails.

**Ownership:** The Node keeps a reference to spec until it closes.

**Blocking:** Sends request 54 (register) and the descriptor in request 56 fragments, each waiting up to control_timeout_ms. Android registers the HID after the last fragment, without telling the host: a report sent at once may STALL or be lost, so wait a little before the first one ([Open sequence](#open-sequence)).

**Threads:** Context domain.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for a null argument, a wrong struct_size or a reservation that leaves no shared slot; `AOAHID_ERR_UNSET_FIELD` for a zero timeout or a reservation that is not 0/0 or 1/0..1; `AOAHID_ERR_OVERFLOW` when the Spec exceeds a policy of the Device or no AOA HID ID is left; a transport error from the requests (`AOAHID_ERR_STALL`: the device has no AOA 2.0 HID).

#### aoahid_node_close

```c
aoahid_result aoahid_node_close(aoahid_node* node, uint32_t control_timeout_ms, uint32_t drain_timeout_ms);
```

- `node`: the Node.
- `control_timeout_ms`: the deadline of each AOA control request, in milliseconds; zero is `AOAHID_ERR_UNSET_FIELD`.
- `drain_timeout_ms`: how long to wait for transfers in flight, in milliseconds (see Blocking); zero is `AOAHID_ERR_UNSET_FIELD`.

**Ownership:** Follows the [close rule](#close-and-destroy): `AOAHID_CLOSE_PENDING` while a report of the Node is still in flight at the drain deadline.

**Blocking:** Finishes a touch frame that has started, sends the neutral report (keys and buttons up, contacts lifted, axes at rest) within drain_timeout_ms, then request 55 (unregister) within control_timeout_ms. A failed step does not stop the next one.

**Threads:** Context domain; not from the log sink.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for a null or closed Node; `AOAHID_ERR_UNSET_FIELD` for a zero timeout; `AOAHID_CLOSE_PENDING`; otherwise the first error on the way out, such as a report that failed earlier or a refused request 55.

#### aoahid_node_id

```c
uint16_t aoahid_node_id(const aoahid_node* node);
```

- `node`: the Node.

**Ownership:** Nothing.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** The Node's AOA HID ID (nonzero) with `AOAHID_OK` recorded, or 0 with `AOAHID_ERR_PARAM` recorded for a null or closed Node.

#### aoahid_node_manifest

```c
aoahid_result aoahid_node_manifest(const aoahid_node* node, aoahid_capability_manifest* manifest);
```

- `node`: the Node.
- `manifest`: the caller's manifest, with `struct_size` set (`aoahid_capability_manifest_init`) and `reserved` zero.

**Ownership:** As `aoahid_spec_manifest`; manifest->reports is valid while the Node is open.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for a null pointer, a wrong struct_size or a nonzero reserved.

#### aoahid_node_submit

```c
aoahid_result aoahid_node_submit(aoahid_node* node, uint32_t send_timeout_ms);
```

- `node`: the Node.
- `send_timeout_ms`: the deadline of the queued request-57 transfer, in milliseconds; zero is `AOAHID_ERR_UNSET_FIELD`.

**Ownership:** The report is built in the library's own transfer buffer.

**Blocking:** Queues one report of the Node's current state, if it changed, and returns. In `AOAHID_EVENT_CALLER_POLL` mode it then handles ready libusb events once without waiting. send_timeout_ms is the deadline of that transfer; its failure comes back from a later call.

**Threads:** Context domain.

**Returns:** `AOAHID_OK`, also when there was nothing to send; `AOAHID_ERR_PARAM` for an invalid Node; `AOAHID_ERR_UNSET_FIELD` for a zero timeout; `AOAHID_ERR_UNSUPPORTED` for a Raw Node (use `aoahid_raw_submit`); `AOAHID_ERR_BUSY` when the Node's report is still in flight or the pool is full; a transport error of this or an earlier transfer.

#### aoahid_node_submit_blocking

```c
aoahid_result aoahid_node_submit_blocking(aoahid_node* node, uint32_t timeout_ms);
```

- `node`: the Node.
- `timeout_ms`: the deadline of the whole call, in milliseconds; zero is `AOAHID_ERR_UNSET_FIELD`.

**Ownership:** As `aoahid_node_submit`.

**Blocking:** Sends reports until the Node's state has been delivered, a mouse's pending movement included, or timeout_ms has passed. Each report's deadline is what is left of timeout_ms. In `AOAHID_EVENT_CALLER_POLL` mode this call handles libusb events.

**Threads:** Context domain.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for an invalid Node; `AOAHID_ERR_UNSET_FIELD` for a zero timeout; `AOAHID_ERR_TIMEOUT` when timeout_ms passed first; a transport error of a transfer.

### Input

These calls change a Node's state; `aoahid_node_submit` sends it. None of
them blocks, allocates or does I/O. Each belongs to the Context domain and
works only on a Node of its own profile. A press and the release after it are
never merged: until the report carrying the press has completed, the release
is refused with `AOAHID_ERR_BUSY`, so a quick tap is not lost. "As every input
call" below means: `AOAHID_OK`; `AOAHID_ERR_PARAM` for a Node of another
profile or a value out of range; `AOAHID_ERR_BUSY` while the Node's report is
in flight or an opposite edge is unsent; `AOAHID_ERR_NO_DEVICE` once the
device is gone.

#### aoahid_keyboard_key

```c
aoahid_result aoahid_keyboard_key(aoahid_node* node, uint16_t usage, uint32_t down);
```

Presses (down 1) or releases (down 0) one Keyboard page Usage, a modifier or a key of the Spec's range. Closing the Node releases every key still down.

- `node`: the Node.
- `usage`: a Keyboard page Usage: a modifier (0xE0 to 0xE7) or one of the Spec's range.
- `down`: 1 presses, 0 releases.

**Ownership:** Nothing.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** As every input call, `AOAHID_OK` through `AOAHID_ERR_NO_DEVICE`.

#### aoahid_mouse_move

```c
aoahid_result aoahid_mouse_move(aoahid_node* node, int32_t dx, int32_t dy);
```

Adds to the pending X and Y movement. A report carries as much as the fields hold and keeps the rest for the next one.

- `node`: the Node.
- `dx`: X movement added to the pending total.
- `dy`: Y movement added to the pending total.

**Ownership:** Nothing.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** As every input call, `AOAHID_OK` through `AOAHID_ERR_NO_DEVICE`; `AOAHID_ERR_OVERFLOW` when the 64-bit pending total overflows.

#### aoahid_mouse_scroll

```c
aoahid_result aoahid_mouse_scroll(aoahid_node* node, int32_t wheel, int32_t pan);
```

Adds to the pending wheel and pan movement, as `aoahid_mouse_move` does.

- `node`: the Node.
- `wheel`: wheel movement added to the pending total.
- `pan`: pan movement added to the pending total.

**Ownership:** Nothing.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** As every input call, `AOAHID_OK` through `AOAHID_ERR_NO_DEVICE`; `AOAHID_ERR_UNSUPPORTED` for a nonzero value on a field the Spec lacks; `AOAHID_ERR_OVERFLOW` when the 64-bit pending total overflows.

#### aoahid_mouse_button

```c
aoahid_result aoahid_mouse_button(aoahid_node* node, uint32_t button, uint32_t down);
```

Presses (down 1) or releases (down 0) button 1 to button_count.

- `node`: the Node.
- `button`: the button, from 1 to the Spec's `button_count`.
- `down`: 1 presses, 0 releases.

**Ownership:** Nothing.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** As every input call, `AOAHID_OK` through `AOAHID_ERR_NO_DEVICE`.

#### aoahid_toggle_usage

```c
aoahid_result aoahid_toggle_usage(aoahid_node* node, uint16_t usage, uint32_t down);
```

Presses (down 1) or releases (down 0) one Usage of the Spec's allow-list; any number may be pressed at once. Closing the Node releases every Usage still down.

- `node`: the Node.
- `usage`: a Usage of the Spec's allow-list.
- `down`: 1 presses, 0 releases.

**Ownership:** Nothing.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** As every input call, `AOAHID_OK` through `AOAHID_ERR_NO_DEVICE`.

#### aoahid_gamepad_button

```c
aoahid_result aoahid_gamepad_button(aoahid_node* node, uint32_t button, uint32_t down);
```

Presses (down 1) or releases (down 0) button 1 to button_count.

- `node`: the Node.
- `button`: the button, from 1 to the Spec's `button_count`.
- `down`: 1 presses, 0 releases.

**Ownership:** Nothing.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** As every input call, `AOAHID_OK` through `AOAHID_ERR_NO_DEVICE`.

#### aoahid_gamepad_axis

```c
aoahid_result aoahid_gamepad_axis(aoahid_node* node, size_t axis_index, int32_t value);
```

Sets axes[axis_index] of the Spec to value, which must be in its logical range.

- `node`: the Node.
- `axis_index`: the index of the axis in the Spec's `axes`.
- `value`: the value, in that axis's logical range.

**Ownership:** Nothing.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** As every input call, `AOAHID_OK` through `AOAHID_ERR_NO_DEVICE`.

#### aoahid_gamepad_dpad

```c
aoahid_result aoahid_gamepad_dpad(aoahid_node* node, uint32_t up, uint32_t down, uint32_t right, uint32_t left);
```

Sets the D-pad from four 0/1 directions. With a Hat, up and down (or left and right) together are refused and no direction sends the Null state; with four buttons the bits go out as given.

- `node`: the Node.
- `up`: 0 or 1.
- `down`: 0 or 1.
- `right`: 0 or 1.
- `left`: 0 or 1.

**Ownership:** Nothing.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** As every input call, `AOAHID_OK` through `AOAHID_ERR_NO_DEVICE`; `AOAHID_ERR_PARAM` also when the Spec has no D-pad.

#### aoahid_touchscreen_contact

```c
aoahid_result aoahid_touchscreen_contact(aoahid_node* node, uint32_t contact_id, uint32_t down, int32_t x, int32_t y, const aoahid_touchscreen_extra* extra);
```

down 1 places contact_id in a free slot, or moves it if it is already down; down 0 lifts it at x, y. A lifted contact_id can be placed again once its lift has been sent. While a frame split over several reports is being sent, every call is refused with `AOAHID_ERR_BUSY`.

- `node`: the Node.
- `contact_id`: the contact's ID.
- `down`: 1 places or moves the contact, 0 lifts it.
- `x`: X of the contact.
- `y`: Y of the contact.
- `extra`: the optional values of the contact, or null.

**Ownership:** Copies extra, which may be null (every optional field 0).

**Blocking:** None.

**Threads:** Context domain.

**Returns:** As every input call, `AOAHID_OK` through `AOAHID_ERR_NO_DEVICE`; `AOAHID_ERR_PARAM` also for lifting a contact_id that is not down; `AOAHID_ERR_BUSY` also for placing one whose lift is unsent; `AOAHID_ERR_OVERFLOW` when every slot is taken.

#### aoahid_touchpad_contact

```c
aoahid_result aoahid_touchpad_contact(aoahid_node* node, uint32_t contact_id, uint32_t down, int32_t x, int32_t y, const aoahid_touchpad_extra* extra);
```

As `aoahid_touchscreen_contact`, for a Touchpad Node.

- `node`: the Node.
- `contact_id`: the contact's ID.
- `down`: 1 places or moves the contact, 0 lifts it.
- `x`: X of the contact.
- `y`: Y of the contact.
- `extra`: the optional values of the contact, or null.

**Ownership:** Copies extra, which may be null.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** As `aoahid_touchscreen_contact`, `AOAHID_OK` through `AOAHID_ERR_OVERFLOW`.

#### aoahid_touchpad_button

```c
aoahid_result aoahid_touchpad_button(aoahid_node* node, uint32_t button, uint32_t down);
```

Presses (down 1) or releases (down 0) click button 1 to button_count.

- `node`: the Node.
- `button`: the button, from 1 to the Spec's `button_count`.
- `down`: 1 presses, 0 releases.

**Ownership:** Nothing.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** As every input call, `AOAHID_OK` through `AOAHID_ERR_NO_DEVICE`.

#### aoahid_pen_update

```c
aoahid_result aoahid_pen_update(aoahid_node* node, const aoahid_pen_sample* sample);
```

Sets the whole pen state. in_range 0 takes the pen away: In Range, Tip, pressure, Invert and the barrel bits go out as 0 with the sample's own X, Y, tilt and twist, so repeat the last position to leave the pointer where it was. Switching between tip and eraser while in range first sends an out-of-range report.

- `node`: the Node.
- `sample`: the whole tool state.

**Ownership:** Copies sample.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** As every input call, `AOAHID_OK` through `AOAHID_ERR_NO_DEVICE`.

#### aoahid_digitizer_update

```c
aoahid_result aoahid_digitizer_update(aoahid_node* node, const aoahid_digitizer_sample* sample);
```

As `aoahid_pen_update`, for a Digitizer Node.

- `node`: the Node.
- `sample`: the whole tool state.

**Ownership:** Copies sample.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** As every input call, `AOAHID_OK` through `AOAHID_ERR_NO_DEVICE`.

#### aoahid_battery_update

```c
aoahid_result aoahid_battery_update(aoahid_node* node, int32_t strength);
```

Sets the battery level of a Node whose Spec has enable_battery. It goes out with the next `aoahid_node_submit` that has no input report to send, and only when it changed. A level not yet sent is dropped on close.

- `node`: the Node.
- `strength`: the level, in the logical range of the Spec's `battery.strength`.

**Ownership:** Nothing.

**Blocking:** None.

**Threads:** Context domain.

**Returns:** As every input call, `AOAHID_OK` through `AOAHID_ERR_NO_DEVICE`; `AOAHID_ERR_PARAM` also for a Node without a battery report.

#### aoahid_raw_submit

```c
aoahid_result aoahid_raw_submit(aoahid_node* node, const uint8_t* report, size_t length, uint32_t send_timeout_ms);
```

- `node`: the Node.
- `report`: the report as it goes on the wire, with its Report ID byte when it has one.
- `length`: its length in bytes.
- `send_timeout_ms`: the deadline of the queued request-57 transfer, in milliseconds; zero is `AOAHID_ERR_UNSET_FIELD`.

**Ownership:** Copies report.

**Blocking:** Queues the report and returns; send_timeout_ms is the deadline of its transfer.

**Threads:** Context domain.

**Returns:** `AOAHID_OK`; `AOAHID_ERR_PARAM` for a Node that is not Raw, a null report, a Report ID or length the Spec does not accept, or nonzero padding bits; `AOAHID_ERR_UNSET_FIELD` for a zero timeout; `AOAHID_ERR_BUSY` while the Node's report is in flight or the pool is full; a transport error of this or an earlier transfer.

## Differences from `libmimhid` {#differences-from-libmimhid}

[`libmimhid`](https://github.com/nemarpuc/libmimhid) is the sister library
that sends the same kind of input to any USB host through a microcontroller
board on a serial port. The profile factories, the input calls, the options
layout, the close rule and the timeout rule have the same shape
(`aoahid_keyboard_key` is `mimhid_keyboard_key`). The table lists what differs;
the libmimhid names are from its header, `mimhid.h`.

| Area | libaoahid | libmimhid |
| --- | --- | --- |
| Target and transport | An Android device over USB through libusb: AOA requests 51 to 57 on endpoint 0 | Any USB host, through a board the host drives over a serial port |
| Handles | `aoahid_context` drives libusb (`aoahid_context_poll`, or an internal event thread), with Devices, Nodes and Channels under it | No Context: `mimhid_device` is the top-level handle, with Nodes under it |
| `*_init` | Fill in working values; `AOAHID_CONTROL_TIMEOUT_MS`, `AOAHID_SEND_TIMEOUT_MS` and `AOAHID_DRAIN_TIMEOUT_MS` are working timeouts | Zero the structure and set `struct_size` only; a required field left zero is `MIMHID_ERR_UNSET_FIELD` |
| USB identity | The phone's own; `aoahid_accessory_start` sends only the AOA strings 0 to 5 | The caller's: `mimhid_device_config` and `mimhid_node_config` carry the USB descriptors the board presents |
| Opening a Node | `aoahid_node_open` registers the HID at once (requests 54 and 56) within `control_timeout_ms`; `aoahid_node_close` sends request 55 | `mimhid_node_open` and `mimhid_node_close` take `mimhid_commit_timeouts` (`control_timeout_ms`, `reconnect_timeout_ms`) because the board enumerates again; `mimhid_device_batch_begin` and `mimhid_device_batch_end` group several changes |
| Submitting | `aoahid_node_submit` queues an asynchronous transfer; its failure comes back from a later call | `mimhid_node_submit` writes to the serial port and returns once the driver has the bytes |
| Direction | Input reports only; Bulk `aoahid_channel` reaches other interfaces such as ADB | Two-way HID: Output and Feature reports reach `event_sink`; `mimhid_node_set_feature_response`, `mimhid_device_set_control_routing` and `mimhid_device_control_reply` answer the host |
| Closing | A close can return `AOAHID_CLOSE_PENDING` while a USB transfer is in flight; the handle stays the caller's | No pending close: writes finish inside the call |
| Result numbers | 4 is `AOAHID_ERR_NOT_AOA`, 9 is `AOAHID_ERR_STALL`, 15 is `AOAHID_CLOSE_PENDING`; 5 is unused | 4 is `MIMHID_ERR_PROTOCOL`; 5, 9 and 15 are unused. Every other number means the same in both |
