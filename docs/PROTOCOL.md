# AOA 2.0 HID Protocol

Android Open Accessory (AOA) 2.0 lets a USB host act as one or more HID input
devices (keyboard, mouse, touchscreen, game controller, …) for an Android
device that is the USB peripheral. This document explains the protocol as
libaoahid uses it, which kernel and framework symbols implement each step on
Android, and which library function sends what.

## Control requests

All requests are vendor requests to the device on endpoint 0.
`bmRequestType` is `0xC0` (`USB_DIR_IN | USB_TYPE_VENDOR`, IN | vendor | device)
or `0x40` (`USB_DIR_OUT | USB_TYPE_VENDOR`, OUT | vendor | device)
([AOA 1.0](https://source.android.com/docs/core/interaction/accessories/aoa),
section "Attempt to start in accessory mode";
[AOA 2.0](https://source.android.com/docs/core/interaction/accessories/aoa2),
section "HID support"; Android common kernel
`drivers/usb/gadget/function/f_accessory.c`, `acc_ctrlrequest`, commits
`0e3db17d01c94263b629b089c51c7d0988308232` and
`9b03ed2feb9c4cc3f15d44eda080caabb3a6b843`, retrieved 2026-08-27).

| bRequest | Name | Dir | wValue | wIndex | Data |
| --- | --- | --- | --- | --- | --- |
| 51 | ACCESSORY_GET_PROTOCOL | IN | 0 | 0 | 2 bytes: protocol version (little-endian). 0 = no AOA; 2 or more needed for HID. |
| 52 | ACCESSORY_SEND_STRING | OUT | 0 | String ID: 0 manufacturer, 1 model, 2 description, 3 version, 4 URI, 5 serial | NUL-terminated string |
| 53 | ACCESSORY_START | OUT | 0 | 0 | none |
| 54 | ACCESSORY_REGISTER_HID | OUT | HID ID (host-chosen) | Total report descriptor length | none |
| 55 | ACCESSORY_UNREGISTER_HID | OUT | HID ID | 0 | none |
| 56 | ACCESSORY_SET_HID_REPORT_DESC | OUT | HID ID | Byte offset of this fragment in the descriptor | Descriptor fragment |
| 57 | ACCESSORY_SEND_HID_EVENT | OUT | HID ID | 0 | One HID input report |

Requests 51–53 (`ACCESSORY_GET_PROTOCOL`, `ACCESSORY_SEND_STRING`,
`ACCESSORY_START`) and string indices 0–5
(`ACCESSORY_STRING_MANUFACTURER`..`ACCESSORY_STRING_SERIAL`) are defined in
[AOA 1.0](https://source.android.com/docs/core/interaction/accessories/aoa) and
`include/uapi/linux/usb/f_accessory.h`; requests 54–57
(`ACCESSORY_REGISTER_HID`, `ACCESSORY_UNREGISTER_HID`,
`ACCESSORY_SET_HID_REPORT_DESC`, `ACCESSORY_SEND_HID_EVENT`) are defined in
[AOA 2.0](https://source.android.com/docs/core/interaction/accessories/aoa2)
and `include/uapi/linux/usb/f_accessory.h`. The kernel driver answers request
51 in `acc_ctrlrequest` with `PROTOCOL_VERSION`, which is `2` in
`drivers/usb/gadget/function/f_accessory.c` (commits
`0e3db17d01c94263b629b089c51c7d0988308232` and
`9b03ed2feb9c4cc3f15d44eda080caabb3a6b843`, retrieved 2026-08-27).

## HID flow

1. **Register** (54, `ACCESSORY_REGISTER_HID`) an ID and announce the descriptor
   length. In `f_accessory.c`, `acc_ctrlrequest` invokes `acc_register_hid(dev,
   w_value, w_index)`, which allocates an `acc_hid_dev` with the given `hid_id`
   and `report_desc_len` and places it on `dev->new_hid_list` (commit
   `0e3db17d01c94263b629b089c51c7d0988308232`, retrieved 2026-08-27).
2. **Send the descriptor** (56, `ACCESSORY_SET_HID_REPORT_DESC`) in one or more
   fragments. In `acc_ctrlrequest` and `acc_hid_set_report_desc` (completion
   callback `acc_complete_set_hid_report_desc`), the kernel rejects a fragment
   unless its offset (`wIndex`) equals the bytes received so far
   (`report_desc_offset`) and `offset + length <= report_desc_len`. When
   `report_desc_offset == report_desc_len`, the completion schedules
   `acc_hid_work` (`INIT_WORK(&dev->hid_work, acc_hid_work)`), which calls
   `acc_hid_init` to create and register the kernel `hid_device` (commit
   `0e3db17d01c94263b629b089c51c7d0988308232`, retrieved 2026-08-27).
3. **Send events** (57, `ACCESSORY_SEND_HID_EVENT`), each one an input report as
   defined by the descriptor. In `acc_ctrlrequest` and
   `acc_complete_send_hid_event`, the kernel looks up the `acc_hid_dev` on
   `dev->hid_list` and rejects an ID whose `hid_device` has not yet been
   registered by `acc_hid_work` (returning `-EINVAL`, which stalls EP0), or
   passes the payload to `hid_input_report(hid->hid, HID_INPUT_REPORT, ...)`
   (commit `0e3db17d01c94263b629b089c51c7d0988308232`, retrieved 2026-08-27).
4. **Unregister** (55, `ACCESSORY_UNREGISTER_HID`) when done. `acc_ctrlrequest`
   calls `acc_unregister_hid(dev, w_value)`, which moves the entry to
   `dev->dead_hid_list` and schedules `acc_hid_work` to call `acc_hid_delete`
   (`hid_destroy_device`). The kernel also removes all registered HID devices
   in `acc_disconnect` when the USB connection goes away (commit
   `0e3db17d01c94263b629b089c51c7d0988308232`, retrieved 2026-08-27).

Several IDs can be registered at once on `dev->hid_list`, each with its own
descriptor ([AOA 2.0](https://source.android.com/docs/core/interaction/accessories/aoa2),
section "HID support").

## Accessory mode vs. the current USB mode

Requests 52 and 53 switch the phone into accessory mode: it disconnects and
re-enumerates with Google's vendor ID `0x18D1` (`USB_ACCESSORY_VENDOR_ID`) and a
product ID in `0x2D00`–`0x2D05` (`0x2D00` `USB_ACCESSORY_PRODUCT_ID`, `0x2D01`
`USB_ACCESSORY_ADB_PRODUCT_ID` in `include/uapi/linux/usb/f_accessory.h` and
[AOA 1.0](https://source.android.com/docs/core/interaction/accessories/aoa);
`0x2D02`–`0x2D05` for audio variants in
[AOA 2.0](https://source.android.com/docs/core/interaction/accessories/aoa2),
retrieved 2026-08-27). Accessory mode is what an Android app matching the
accessory strings needs. If the host does not configure the re-enumerated
accessory within 10 seconds, Android's USB service cancels the accessory mode
switch (AOSP `platform/frameworks/base`,
`services/usb/java/com/android/server/usb/UsbDeviceManager.java`,
`ACCESSORY_REQUEST_TIMEOUT = 10000`, commit
`15c8b135d2c41940f53aa77f3d48354bc2e7e2d7`, retrieved 2026-09-23).

HID requests 54–57 go to endpoint 0, and `f_accessory.c` handles them in
`acc_ctrlrequest` alongside requests 51–53 without checking whether the
accessory bulk function is `online`. The driver also exports
`acc_ctrlrequest_composite` (`drivers/usb/gadget/function/f_accessory.c`,
commits `0e3db17d01c94263b629b089c51c7d0988308232` and
`9b03ed2feb9c4cc3f15d44eda080caabb3a6b843`, retrieved 2026-08-27), through which
a composite USB gadget forwards control requests to `acc_ctrlrequest` even when
the accessory function is not in the active USB configuration. Whether a given
device answers requests 54–57 in its normal USB mode (for example MTP or ADB)
depends on its kernel gadget configuration. If a device does not,
`aoahid_node_open` fails (typically with `AOAHID_ERR_STALL`), and you can
switch it to accessory mode first.

## Input only

The host sends Input reports only. In `drivers/usb/gadget/function/f_accessory.c`,
the kernel's `hid_ll_driver` (`acc_hid_ll_driver`) sets `.raw_request =
acc_hid_raw_request`, which does nothing and returns `0` (commit
`0e3db17d01c94263b629b089c51c7d0988308232`, retrieved 2026-08-27), and
[AOA 2.0](https://source.android.com/docs/core/interaction/accessories/aoa2)
defines no control request or endpoint that carries Output or Feature reports
back to the host. So keyboard LED state, force feedback, and Feature reports
(such as a touchscreen's maximum contact count) never reach the host.
libaoahid reflects this: capability manifests report `output_supported = 0` and
`feature_supported = 0`, and Specs are built around Input reports.

## How Android sees the device

In `drivers/usb/gadget/function/f_accessory.c`, `acc_hid_init` allocates a
`hid_device` via `hid_allocate_device()`, sets `hid->ll_driver =
&acc_hid_ll_driver` and `hid->bus = BUS_USB`, and calls `hid_add_device(hid)`;
`acc_hid_parse` then passes the received report descriptor to `hid_parse_report`
(commit `0e3db17d01c94263b629b089c51c7d0988308232`, retrieved 2026-08-27). From
there the Linux HID core (`drivers/hid/hid-core.c`, `drivers/hid/hid-input.c`,
`drivers/hid/hid-multitouch.c`, commit
`35556bed836f8dc07ac55f69c8d17dce3e7f0e25`, retrieved 2026-08-27) binds a HID
driver and creates `evdev` `/dev/input/event*` nodes. Android's
`EventHub::openDeviceLocked` (`frameworks/native/services/inputflinger/reader/EventHub.cpp`)
opens those `evdev` nodes and assigns input device classes, and
`InputReader::createDeviceLocked`
(`frameworks/native/services/inputflinger/reader/InputReader.cpp`) attaches the
input mappers that turn kernel events into Android `KeyEvent` and `MotionEvent`
streams ([Android Input overview](https://source.android.com/docs/core/interaction/input),
retrieved 2026-08-27). Which kernel driver binds, and how Android classifies the
device, depends on the descriptor and on that device's kernel and Android
version. See [PROFILES.md](PROFILES.md) and [TARGET_MATRIX.md](TARGET_MATRIX.md).

## Host-side caveats

- **Windows driver.** On Windows, `libusb`'s WinUSB backend requires `WinUSB`,
  `libusbK`, or `libusb0` (`libusb-win32`) bound to the device or interface
  (`libusb` v1.0.30, `libusb/os/windows_winusb.c`, `winusbx_driver_names` and
  `winusbx_claim_interface`, commit `87a55632db62c9bdc58cd31d3ccfa673f1bb017f`,
  retrieved 2026-08-27; [libusb Windows wiki](https://github.com/libusb/libusb/wiki/Windows),
  section "Driver Installation", retrieved 2026-08-27). The phone's normal USB
  interfaces usually bind MTP or OEM drivers, and the accessory-mode device (a
  different product ID, `0x2D00`/`0x2D01`) needs its own binding.
- **One process per device on Windows.** `WinUSB` opens the device for exclusive
  access by a single application at a time
  ([Microsoft WinUSB documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/winusb),
  section "Access a USB device by using WinUSB functions", retrieved 2026-08-27;
  [libusb Windows wiki](https://github.com/libusb/libusb/wiki/Windows), section
  "Known Restrictions", retrieved 2026-08-27). Inside one process, libaoahid
  shares a single `libusb_device_handle` (`Port`) between the Device, its
  Channels, and discovery.
- **Linux permissions.** On Linux, `libusb` opens `/dev/bus/usb/<bus>/<dev>`
  with `O_RDWR` (`libusb` v1.0.30, `libusb/os/linux_usbfs.c`, `op_open`, commit
  `87a55632db62c9bdc58cd31d3ccfa673f1bb017f`, retrieved 2026-08-27), so the user
  needs write access to the `usbfs` device node, usually through a `udev` rule
  such as `udev/51-aoahid.rules`.
- **libusb version.** The library requires `libusb` runtime 1.0.30 or later in
  the 1.0 API line (`libusb_get_version()`, `src/transport/transport.cpp`) and
  fails `aoahid_context_create` with `AOAHID_ERR_UNSUPPORTED` otherwise.

## What libaoahid does, and where

| Step | Function | Requests |
| --- | --- | --- |
| Find AOA-capable devices | `aoahid_discover` | 51 to every accessible USB device. Lists only those with a nonzero version, with the version in `protocol_version`. The caller decides whether version 2 is required. |
| Switch to accessory mode (optional) | `aoahid_accessory_start` | 51, then 52 for each non-null string (manufacturer and model required), then 53. Returns when 53 completes. It does not wait for re-enumeration and never retries. A device already in accessory mode receives nothing. |
| Open the device | `aoahid_device_open` | None. Opens the device in its current mode and allocates the transfer pool. |
| Register a HID device | `aoahid_node_open` | 54, then 56 in fragments of `descriptor_fragment_bytes`. On failure after 54, sends a best-effort 55. |
| Send input | `aoahid_node_submit`, `aoahid_node_submit_blocking`, `aoahid_raw_submit` | 57, asynchronous. |
| Unregister | `aoahid_node_close`, `aoahid_device_close`, context destroy | Neutral reports as needed, then 55. |
| Bulk data (e.g. ADB) | `aoahid_channel_open/read/write` | No AOA request. Bulk transfers on an interface chosen by class/subclass/protocol. |

The request implementations are in `src/transport/discovery.cpp` (51 probe),
`src/transport/port.cpp` (51–53), and `src/transport/transport.cpp` (54–57).

## Primary sources

- **Android Open Accessory Protocol 1.0** ("Attempt to start in accessory
  mode": requests 51–53, string indices 0–5, Google VID `0x18D1`, PIDs
  `0x2D00` and `0x2D01`):
  <https://source.android.com/docs/core/interaction/accessories/aoa>, retrieved
  2026-08-27.
- **Android Open Accessory Protocol 2.0** ("HID support": requests 54–57;
  "Audio support": PIDs `0x2D02`–`0x2D05`):
  <https://source.android.com/docs/core/interaction/accessories/aoa2>, retrieved
  2026-08-27.
- **Android common kernel AOA gadget driver**,
  [`include/uapi/linux/usb/f_accessory.h`](https://android.googlesource.com/kernel/common/+/0e3db17d01c94263b629b089c51c7d0988308232/include/uapi/linux/usb/f_accessory.h)
  and
  [`drivers/usb/gadget/function/f_accessory.c`](https://android.googlesource.com/kernel/common/+/0e3db17d01c94263b629b089c51c7d0988308232/drivers/usb/gadget/function/f_accessory.c),
  commits `0e3db17d01c94263b629b089c51c7d0988308232` and
  `9b03ed2feb9c4cc3f15d44eda080caabb3a6b843` (`PROTOCOL_VERSION`,
  `acc_ctrlrequest`, `acc_ctrlrequest_composite`, `acc_register_hid`,
  `acc_unregister_hid`, `acc_hid_set_report_desc`,
  `acc_complete_set_hid_report_desc`, `acc_complete_send_hid_event`,
  `acc_hid_work`, `acc_hid_init`, `acc_hid_delete`, `acc_hid_raw_request`,
  `acc_hid_ll_driver`, `acc_disconnect`), retrieved 2026-08-27.
- **Android common kernel HID subsystem**, commit
  `35556bed836f8dc07ac55f69c8d17dce3e7f0e25` (2020-09-01, `include/linux/hid.h`,
  `drivers/hid/hid-core.c`, `drivers/hid/hid-input.c`,
  `drivers/hid/hid-multitouch.c`), retrieved 2026-08-27.
- **Android Open Source Project (AOSP) USB service & InputFlinger**:
  - `platform/frameworks/base`,
    [`services/usb/java/com/android/server/usb/UsbDeviceManager.java`](https://android.googlesource.com/platform/frameworks/base/+/15c8b135d2c41940f53aa77f3d48354bc2e7e2d7/services/usb/java/com/android/server/usb/UsbDeviceManager.java),
    commit `15c8b135d2c41940f53aa77f3d48354bc2e7e2d7`
    (`ACCESSORY_REQUEST_TIMEOUT`), retrieved 2026-09-23.
  - `platform/frameworks/native`,
    `services/inputflinger/reader/EventHub.cpp` (`EventHub::openDeviceLocked`)
    and `services/inputflinger/reader/InputReader.cpp`
    (`InputReader::createDeviceLocked`), and
    [Android Input overview](https://source.android.com/docs/core/interaction/input),
    retrieved 2026-08-27.
- **libusb v1.0.30**, commit `87a55632db62c9bdc58cd31d3ccfa673f1bb017f`:
  [`libusb/os/windows_winusb.c`](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/windows_winusb.c)
  (`winusbx_driver_names`, `winusbx_claim_interface`,
  `WinUsb_GetAssociatedInterface`) and
  [`libusb/os/linux_usbfs.c`](https://github.com/libusb/libusb/blob/87a55632db62c9bdc58cd31d3ccfa673f1bb017f/libusb/os/linux_usbfs.c)
  (`op_open`), retrieved 2026-08-27.
- **libusb Windows backend wiki** ("Driver Installation" and "Known
  Restrictions"): <https://github.com/libusb/libusb/wiki/Windows>, retrieved
  2026-08-27.
- **Microsoft WinUSB documentation** ("Access a USB device by using WinUSB
  functions"):
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/winusb>,
  retrieved 2026-08-27.

## Hardware status

Which devices, hosts, and profiles have been confirmed on real hardware is
recorded in [TARGET_MATRIX.md](TARGET_MATRIX.md).

See also: [README.md](https://github.com/nemarpuc/libaoahid#readme), [ARCHITECTURE.md](ARCHITECTURE.md),
[QUICKSTART.md](QUICKSTART.md), [LIMITS.md](LIMITS.md).
