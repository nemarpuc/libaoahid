# Internals

How the implementation behind `include/aoahid.h` works: which object owns
what, which lock guards what, and what runs on each path. It is for people
changing the library. The public model is in [ARCHITECTURE.md](ARCHITECTURE.md),
the wire protocol in [PROTOCOL.md](PROTOCOL.md), and every limit in
[LIMITS.md](LIMITS.md); this page does not repeat them.

## Handles

| Handle | Definition | Holds |
| --- | --- | --- |
| `aoahid_context` | `src/api/internal.hpp` | The `Runtime` (libusb context), the event thread, `devices`, `graveyard`, the HID ID domains, and the deferred error latch. |
| `aoahid_device` | `src/api/internal.hpp` | A `transport::Device` (transfer pool and one `Port` reference), its policy values, `nodes`, `channels`, and the pool-slot signal. |
| `aoahid_node` | `src/api/internal.hpp` | A retained Spec, the `NodeState` variant, the HID ID, the pool reservation, and the completion fields the callback publishes. |
| `aoahid_channel` | `src/api/internal.hpp` | A `transport::Channel` (its own transfers and one `Port` reference). |
| `aoahid_spec` | `src/api/internal.hpp` | An atomic reference count, the descriptor bytes, the `ReportLayout`, the `DescriptorRequirements`, and the copied options. |

A `Port` (`src/transport/transport.hpp`) is the only owner of a libusb device
handle. A Device and each of its Channels hold one reference; the handle
closes when the last is released, which each holder does only after its own
transfers have drained. `Device::open` always opens a new handle
(`src/transport/port.cpp`). The Runtime's Port list exists so discovery and
accessory start can probe a device that is already open through its existing
handle.

No libusb type appears above `src/transport/transport.hpp`.

## Locks

| Lock | Guards | Notes |
| --- | --- | --- |
| `aoahid_context::mutex` | `devices`, `graveyard`, `deferred_error`, `event_thread_error` | Taken through `active_state_mutex`, which is null in caller-poll mode, so `ModeMutexGuard` (`src/api/internal.hpp`) locks nothing there. |
| `aoahid_node::completion_mutex` | The hand-over of a completion to a thread blocked in `aoahid_node_submit_blocking` | Also the callback-exit barrier Node close takes. Null in caller-poll mode. |
| `aoahid_device::slot_mutex` | The wait for a free pool slot | `slot_generation` is bumped by every report completion. |
| Transport `StateGuard` (`src/transport/transport.cpp`) | The transfer pool: `free_stack`, reservations, `outstanding`, `callbacks_active`, the Device error latch | An `atomic_flag` spinlock, taken only in internal-thread mode. `libusb_submit_transfer` is called while it is held. |
| `Runtime::ports_mutex_`, `Port::claims_mutex_` | The open-Port list; the per-interface claim counts | Leaf locks, open and close paths only. |

Order: the Context mutex is taken before the transport spinlock
(`reap_graveyard` in `src/api/context_ops.cpp` calls `graph_drained` under
it). Nothing takes them the other way round.

Profile calls need no lock: a mutator returns `AOAHID_ERR_BUSY` while
`transfer_inflight` is set (`usable` in `src/profiles/state.cpp`), so the
event thread and the application never write Node state at the same time.

## Event thread

Internal-thread mode starts one thread in `aoahid_context_create`
(`src/api/c_api_context.cpp`). Its loop is `Runtime::poll` with
`kInternalEventPollTimeoutMs` (60 000 ms) followed by `reap_graveyard`. A
failed poll latches a Context error and waits 10 ms on `graveyard_cv` before
the next poll.

`Runtime::poll` (`src/transport/discovery.cpp`) is one
`libusb_handle_events_timeout_completed`. An interrupted wait counts as
success.

Stopping sets `stop_event_thread`, then calls
`Runtime::interrupt_event_handler` (`libusb_interrupt_event_handler`), then
joins the thread (`request_event_thread_stop` and `aoahid_context_destroy` in
`src/api/c_api_context.cpp`). The 60-second timeout only bounds a backend that
ignores the interrupt.

The log sink can run on the event thread. Exceptions from it are swallowed.

## Send path

Per report, in internal-thread mode:

1. A profile call changes `NodeState` and marks the Node dirty. Press and
   release of one control cannot share a report: the second edge returns
   `AOAHID_ERR_BUSY` until a report carrying the first has completed.
2. Every entry point in `src/api/c_api_*.cpp` runs
   `preflight_context`, which reads the `event_thread_failed` atomic and then
   checks the deferred error latch under the Context mutex
   (`src/api/context_ops.cpp`).
3. `Device::acquire` (`src/transport/transport.cpp`) pops a slot from
   `free_stack`, a `std::vector<Slot*>` used as a stack (last in, first out).
   A slot is handed out only while more are free than other Nodes have
   reserved, unless the caller is filling its own reservation. It writes only
   wValue (the HID ID) and wLength (`store_le16` at offsets 2 and 6); the rest
   of the setup packet was written at open by `libusb_fill_control_setup`.
4. `serialize_node` (`src/profiles/state.cpp`) writes the report into the
   slot buffer. With `validate_reports` it is decoded again by
   `validate_serialized_report`.
5. `Device::submit` submits the control transfer. In caller-poll mode
   `aoahid_node_submit` then runs one zero-timeout poll; a failure there is
   latched in the Context, not returned.
6. The completion callback (`transfer_complete` in `src/profiles/state.cpp`)
   publishes `completion_result` and then clears `transfer_inflight`. A
   completed length other than the report length is
   `AOAHID_ERR_SHORT_TRANSFER`.

A STALL, or a submit libusb did not accept, keeps the state pending for a
resend: nothing was applied. Any other failed completion (a timeout, a
cancellation) may have reached the device, so a mouse report consumes the
motion it carried and motion is never sent twice.

`aoahid_node_submit_blocking` waits in `poll_until_idle`
(`src/api/node_ops.cpp`): on the Node's `completion_cv`, or on the Device's
`slot_cv` when every pool slot is taken. In internal-thread mode both waits
wake every 10 ms to check for a Context error; in caller-poll mode the loop
runs `Runtime::poll` for 1 ms at a time instead.

## Errors

- Two thread-local records exist: the transport's (`record_error`) and the
  API's (`set_error`). `finish_transport_result` merges them into what
  `aoahid_last_error` returns.
- An asynchronous completion error reaches the caller three ways: the Node's
  `completion_*` atomics (reported by the next submit or close), the Device
  latch (`aoahid_device_latched_error`), and the Context `deferred_error`.
- The Context latch keeps the first error only.
- An event-thread termination is stored separately in `event_thread_error`
  and is returned by every later call.
- `DeferredError` holds only string literals, so it never allocates and can
  outlive the Device it came from.

## Close and the graveyard

`device_close_impl` (`src/api/node_ops.cpp`) marks the
Device closing, closes every Node (neutral report, request 55, reservation)
inside one `drain_timeout_ms` budget (an argument of `aoahid_device_close`),
loses every Channel, cancels all transfers, and waits for them to drain.

- Drained means `outstanding == 0 && callbacks_active == 0`
  (`Device::drained` in `src/transport/transport.cpp`). `callbacks_active` is
  decremented only after the completion callback returns.
- A drained Device is destroyed at once (`device_close_impl`).
- Otherwise the whole graph moves to `graveyard` (`park_in_graveyard` in
  `src/api/context_ops.cpp`) and `aoahid_device_close` still consumes the handle;
  errors of the rest of the teardown come back from `aoahid_context_poll` or
  `aoahid_context_destroy`. The event thread reaps it after a poll returns;
  in caller-poll mode `aoahid_context_poll` and context destroy do. Nothing
  wakes the event thread for the hand-over itself: a graph whose last callback
  finished just before it is reaped at the next USB event, or when the
  60-second wait ends.
- `aoahid_device_open` reserves room in both `devices` and `graveyard`, so
  the hand-over cannot allocate.
- A `transport::Device` or `Channel` destroyed while not drained leaks its
  state on purpose: a late libusb callback must not touch freed memory.

Reaping runs synchronous control transfers (request 55) on the event thread,
between polls.

## Registration

`allocate_hid_id` (`src/api/context_ops.cpp`) takes the next ID of the
Device's domain (USB bus plus port path). IDs start at 1 and are never reused
while the Context lives; once ID 65535 has been handed out, the next
allocation in that domain returns `AOAHID_ERR_OVERFLOW`.

`Device::register_hid` (`src/transport/transport.cpp`) sends request 54 and
then request 56 in fragments. When a request fails in a way the device may
still have accepted (timeout, I/O error, short transfer:
`control_outcome_may_be_accepted`), a best-effort request 55 follows. The Node
is published only after success.

## Channel

`transport::Channel` (`src/transport/channel.cpp`) owns fixed IN and OUT
slots allocated at open. `read`, `write`, and the callback never allocate.

- Completions reach the reader, and free OUT slots reach the writer, through
  single-producer single-consumer rings (`IndexRing`). The callback takes the
  waiter mutex only when a thread is waiting (`Waiter::notify`).
- Stream mode keeps `in_transfers` IN transfers submitted. Request mode has
  one IN slot and submits it from `read`, sized from the caller's capacity and
  rounded up to a whole packet; a timed-out read leaves it pending and the
  next read waits for the same transfer.
- `write` returns once every byte is queued. An OUT completion error is
  stored and returned once by the next `write`. Only `LIBUSB_TRANSFER_NO_DEVICE`
  marks the Channel lost from the OUT side; any failed IN transfer does.
- A lost Channel cancels its transfers and every later call returns
  `AOAHID_ERR_NO_DEVICE`.
- Open selects configuration 1 only when the device is unconfigured: setting
  the active configuration again resets the device (see Sources).
  `aoahid_channel_open` first waits for report transfers to drain.

Behavior to know before changing it:

- The packet size used for rounding and for the zero-length packet rule is
  the larger of the IN and OUT `wMaxPacketSize` (`max_packet`, set in
  `find_bulk_interface`).
- A halted endpoint is not cleared: the library never calls
  `libusb_clear_halt`.
- In stream mode, when a later IN submit fails during open while an earlier
  one is in flight, open returns `AOAHID_OK` for a Channel that is already
  lost; the first read reports `AOAHID_ERR_NO_DEVICE`.

## Descriptor generation

`build_generated_spec` (`src/profiles/spec.cpp`) runs the same emit function
twice: once counting into null storage, once writing into a buffer of exactly
that size. The count is capped at 65535 bytes, the 16-bit AOA length
(`kAoaDescriptorWireMaximum`).

- `ItemWriter` (`src/hid/item_writer.cpp`) writes short items of 0, 1, 2, or
  4 data bytes. Minimum items are signed. A Maximum is unsigned when both
  extents are non-negative.
- `DescriptorBuilder::variable` (`src/hid/descriptor_builder.cpp`) adds one
  field and its `FieldLayout` entry (semantic, bit offset, width, range). It
  first pads to the next byte boundary when the field would otherwise touch a
  fifth byte.
- `pad_report` (`src/profiles/spec.cpp`) pads to a byte boundary at the fixed
  points listed in [LIMITS.md](LIMITS.md).
- `validate_generated` (`src/hid/validator.cpp`) parses the result again and
  checks it against the layout.
- `DescriptorRequirements` records what the descriptor needs of the target
  parser (usages, fields per report, report bits). `aoahid_node_open` compares
  it with the Device's `linux_hid` (`aoahid_linux_hid_policy`) values.

`store_bits` (`src/profiles/state.cpp`) writes a value little-endian at its
layout offset; values outside the logical range are rejected, except the null
value of a Null State field.

## State machines

- Every profile keeps transition marks for edges not yet reported. A report
  completion consumes them.
- Mouse deltas are 64-bit pending totals. A report carries what fits the
  field range; the rest stays pending.
- Touch state is a fixed array of 16 contacts with phases none, down, and up.
  A contact that was lifted still occupies its slot until the report carrying
  the lift completes, so placing one more than `maximum_contacts` returns
  `AOAHID_ERR_OVERFLOW`.
- With multi-packet frames the Node stays busy until the last packet of the
  frame has completed.
- A pen that changes tool while in range first sends one out-of-range report.
- The toggle profile keeps one pressed bit and one edge flag per allowed Usage.

## Design decisions

The API-level decisions are in
[ARCHITECTURE.md](ARCHITECTURE.md#design-decisions); these are the
implementation's, in the same format. Version numbers in the cited commit
subjects predate the restart of the version line at 0.1.0 (commit `5e9e8a7`).

### Setup packet written once per slot

**Decision:** Each pool slot's setup packet is filled at open
(`libusb_fill_control_setup`, request 57, wValue 0); `Device::acquire` writes
only wValue (the HID ID) and wLength per report. Free slots are a
`std::vector<Slot*>` used as a stack (`free_stack`): `acquire` takes the last
one and a completion pushes it back.

**Reason:** Commit `15a6491`: "The transport no longer rewrites the setup
fields acquire() already wrote." Why the free slots are reused last-in
first-out is not recorded.

**Rejected:** Rewriting the unchanging setup fields per submission
(`15a6491`).

### Byte-wise serialization and a plain spinlock

**Decision:** `store_bits` (`src/profiles/state.cpp`) packs and checks report
fields a byte at a time. `StateGuard` (`src/transport/transport.cpp`) is a
plain `std::atomic_flag` `test_and_set` loop around the pool and counter
updates.

**Reason:** Commit `15a6491` measured byte-wise packing as "about 2.9 times
faster for a 16-contact touchscreen report; the bytes on the wire are
unchanged". Commit `0f7739e` reverted a pause/yield backoff in `StateGuard` and
a byte-aligned fast path in `store_bits` because they were "implemented from a
plan document's claims about bottlenecks, not from profiling this codebase".

**Rejected:** The `StateGuard` backoff and the `store_bits` fast path
(`0f7739e`).

### Two-pass descriptor emission and span padding

**Decision:** `build_generated_spec` runs the profile's emit function twice,
first counting and then into a buffer of exactly that size, and
`DescriptorBuilder::variable` pads to the next byte when a field would touch a
fifth byte (HID 1.11 section 8.4, as the code comment says).

**Reason:** Commits `3a7c30d` and `61d7d2c` ("accept every field width
combination"): a valid field of 1 to 32 bits must not be refused because of
where earlier fields ended. Why the length is counted in a first pass instead
of grown is not recorded.

**Rejected:** Refusing width combinations whose offsets made a field cross a
fifth byte (`3a7c30d`, `61d7d2c`).

### Room in the graveyard reserved at open

**Decision:** `aoahid_device_open` reserves room in `context->devices` and
`context->graveyard` (`src/api/c_api_device.cpp`), so moving an undrained
Device graph to the graveyard at close cannot allocate. `aoahid_device_close`
first waits within its `drain_timeout_ms` for the transfers to drain.

**Reason:** The code comment: "This keeps close teardown recoverable even
under OOM." `libusb_cancel_transfer` "returns immediately, but this does not
indicate cancellation is complete"
([libusb asynchronous I/O](https://libusb.sourceforge.io/api-1.0/group__libusb__asyncio.html),
retrieved 2026-11-10), so the graph must outlive the close. Commit
`26bc5c5` made Device close wait for the drain in internal-thread mode
instead of parking a Device that had already drained.

**Rejected:** Parking a drained Device in the graveyard (`26bc5c5`).

### Host state allocated before request 54

**Decision:** In `aoahid_node_open` (`src/api/c_api_node.cpp`),
`initialize_node_state`, `device->nodes.reserve`, `retain_spec` and
`set_reservation` all run before `register_hid` sends request 54, so
publishing the registered Node is a push that cannot allocate.

**Reason:** The code comment: "After Android accepts a registration,
publishing the Node into this pre-reserved vector cannot allocate or orphan
it."


**Rejected:** None recorded.

### One neutralizer function per profile

**Decision:** `make_neutral` (`src/api/node_ops.cpp`) calls a separate
`AOAHID_NOINLINE` function for each `NodeState` alternative
(`neutralize_keyboard`, `neutralize_mouse`, `neutralize_gamepad`,
`neutralize_touch`, `neutralize_pen`).

**Reason:** Commits `297729f`, `26c7153` and `963548f`: with every
alternative inlined into one function, GCC 14 at `-O3` reported a false
`-Wstringop-overflow`, which `-Werror` turns into a build failure. Spelling
the hint `[[gnu::noinline]]` then broke MSVC under `/WX` (C5030 becomes C2220),
so `AOAHID_NOINLINE` expands to `__declspec(noinline)` there and
`__attribute__((noinline))` elsewhere.

**Rejected:** One function with every alternative inlined.

## Tests and checks

- `tests/fake_libusb/` implements the libusb calls the transport uses. It is
  linked only with `AOAHID_USE_FAKE_LIBUSB=ON`, which `AOAHID_BUILD_TESTS=ON`
  requires.
- `aoahid_tests` runs the suites named on its command line (`item_writer`,
  `profiles`, `spec_options`, `raw_descriptor`, `transport`, `identity`,
  `api_arguments`, `report_state`, `transport_failures`); separate `sim`
  executables cover internal-thread mode (`aoahid_sim_internal_thread`),
  allocation failures (`aoahid_sim_allocation_failure`), and the serialization
  check (`aoahid_sim_serialization_check`).
- `tests/golden/*.hex` are the expected descriptors. A generator change that
  alters one of them changes what existing callers put on the wire.
- `tests/abi/` and the binding tests compare the C struct layout with the
  layout oracle and with the Python, C#, and Rust bindings.
- CI formats with clang-format 14 and lints with clang-tidy 14; a newer
  clang-format lays out some lines differently.
- The `tsan` preset (`CMakePresets.json`) builds the tests with
  ThreadSanitizer, the `coverage` preset builds with `--coverage`, and
  `-DAOAHID_SANITIZE=ON` enables AddressSanitizer and UndefinedBehaviorSanitizer.

## Language bindings

`tools/bindings/generate.py` reads `include/aoahid.h` through
`clang -Xclang -ast-dump=json` (structures, function pointer types, exported
functions, enumerations) and `clang -E -dM` (integer macros). It writes the C
layout oracle `tests/abi/layout_oracle.c` and, for each of Python, C#, and Rust,
the declarations (`native.py`, `Native.cs`, `aoahid-sys/src/lib.rs`) and a layout
test. Nothing in them is written by hand, so a change to the header is a
regeneration, not a transcription. libmimusb uses the same file (the two copies
differ only in the copyright line).

- An enumeration is the anonymous `enum` after an `int32_t` typedef. It becomes
  `int32_t` plus constants; in C# an `enum : int` whose members drop the prefix
  they share. `CS_MEMBER_RENAMES` and `TYPE_RENAMES` hold the few names that are
  not the C name with its prefix cut (`Result.Parameter`, `ReportId`,
  `ClaimPolicy`); they exist because those names were already public.
- Only exported functions are bound: `static inline` functions are skipped.
- In C#, a function with a structure pointer has two forms with the same entry
  point: `in`/`ref` for the common case, `nint` where `null` is allowed. An
  array in a structure is a `fixed` buffer, which keeps the structure blittable.
- The layout test of each language runs the oracle and compares every structure
  size, field offset, and constant, and checks that a new options structure has
  `struct_size` 0. `generate.py --check` fails when a committed file differs from
  the header; CI runs it with `CLANG=clang-14`.
- `bindings/python/tests/test_contract.py` is hand-written: what the package
  exports, and that the binding fills no option field.
- The generator refuses what it cannot bind (an unknown type, a field named like
  a Python keyword, two types with one short name) instead of guessing.

## Version and release {#version-and-release}

- `tools/release/version_files.py` lists every file that carries the version.
  `tools/release/sync_version.py X.Y.Z` writes them all;
  `tools/release/validate_release.py` rejects a tag whose files disagree or
  whose `CHANGELOG.md` has no dated section.
- Each CHANGELOG section states whether the release was verified on hardware.
- A pushed `v*` tag runs `.github/workflows/release.yml`, which builds and
  publishes the GitHub Release: one archive per platform with both libaoahid
  flavours, one release SBOM, the source archive, `release-manifest.json`, and
  `SHA256SUMS` (10 assets). Started by hand (`workflow_dispatch`, `dry_run`
  true) the same workflow runs every build and check without a tag, takes the
  version from the newest dated CHANGELOG section, uploads the assembled set as
  the `release-dist` artifact, and publishes nothing. The PyPI and NuGet jobs run only when the
  repository variable `AOAHID_PUBLISH_BINDINGS` is `true`.
- Registry publication also needs setup by the repository owner: PyPI Trusted
  Publishing for the `aoahid` project and workflow, and a scoped `NUGET_API_KEY`
  secret with authority for the `AoaHid` package ID. This source tree does not
  establish registry ownership. Neither package contains native libraries, so
  users still install the version-matched archive from the GitHub Release.
- The same workflow always builds, byte-rebuilds, installs, and validates the
  wheel and the NuGet package before the native GitHub Release job can publish.
  A workflow failure cannot undo an already-pushed tag; it prevents creation of
  the GitHub Release and its assets.
- Downstream projects pin a libaoahid release by tag in their own CI, and
  download its release assets, so a tag must exist before they can move to it.

## Sources

- libusb 1.0 API, `libusb_set_configuration` ("If you call this function on a
  device already configured with the selected configuration, then this
  function will act as a lightweight device reset") and
  `libusb_claim_interface` ("a purely logical operation; it does not cause any
  requests to be sent over the bus"):
  <https://libusb.sourceforge.io/api-1.0/group__libusb__dev.html>, retrieved
  2026-11-10.
