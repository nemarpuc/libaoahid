# Changelog

All notable changes to libaoahid are recorded here. This project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `AOAHID_CHECK_SERIALIZATION` build option (off by default): the library ends
  the process, naming the second call, when two threads are inside calls on one
  Context. A debugging aid for the caller-serialization rule; with it off the
  built library is unchanged. See `docs/ARCHITECTURE.md`.

## [1.0.0] - 2026-10-11

The first stable release. Hardware-verified on a Samsung Galaxy Tab S11
(Arch Linux):
Keyboard, Mouse, Toggle (media keys), Gamepad, Touchscreen, Touchpad, Pen,
and Digitizer were verified via devtest and kernel getevent. A battery report
on a Keyboard was registered there too (the kernel creates the power supply
and Android classes the device `BATTERY`); the reported level was not read
back.


### Changed (breaking)

- A battery level is now a second Input report inside an input profile, not a
  profile of its own. Every generated profile's options end with
  `enable_battery` and `battery` (`aoahid_battery_report`: `report_id`,
  `strength`); when enabled, the profile's Application Collection carries a
  Battery Strength (`0x06/0x20`) report under its own Report ID. Linux drops a
  HID device that has no input usages, so the standalone form never produced a
  power supply.
- `aoahid_spec_create_battery`, `aoahid_battery_options`, and
  `AOAHID_PROFILE_BATTERY` are removed.
- `aoahid_battery_update(node, strength)` takes the level only and works on
  any Node whose Spec has a battery report. There is no "unknown" value:
  Linux drops out-of-range battery values. The report is sent by a submit that
  has no input report to send, and only when the level changed.
- Error fields name the structure the caller filled in: Touchscreen option
  errors are `touchscreen.*` (they were `touch.*`), Digitizer option and
  sample errors are `digitizer.*` (they were `pen.*`), and the contact errors
  shared by Touchscreen and Touchpad carry their own profile's name.
- The profile numbers are reordered: `KEYBOARD` 1, `MOUSE` 2, `TOUCHSCREEN` 3,
  `TOUCHPAD` 4, `PEN` 5, `DIGITIZER` 6, `GAMEPAD` 7, `TOGGLE` 8, `RAW` 9.
- The Pen profile is split in two, like Touch Screen and Touch Pad. `PEN` is
  the direct-screen stylus under Digitizers / Pen (`0x0D/0x02`). The indirect
  tablet form is the new `AOAHID_PROFILE_DIGITIZER` (= 6), created with
  `aoahid_spec_create_digitizer(&aoahid_digitizer_options)`, under Digitizers /
  Digitizer (`0x0D/0x01`).
- `aoahid_pen_options` no longer has a `mode` field. `aoahid_pen_mode`,
  `AOAHID_PEN_DIRECT_SCREEN`, and `AOAHID_PEN_INDIRECT_TABLET` are removed.
- The descriptors are byte-identical to the former `mode` forms; the golden
  file `pen-indirect.hex` is now `digitizer.hex`.
- Every profile that Android recognizes differently has its own send calls and
  structures. `aoahid_touch` is replaced by `aoahid_touchscreen_contact` and
  `aoahid_touchpad_contact`, and `aoahid_touch_extra` by `aoahid_touchscreen_extra`
  and `aoahid_touchpad_extra`. `aoahid_pen_update` takes
  Pen Nodes only; Digitizer Nodes use `aoahid_digitizer_update` and
  `aoahid_digitizer_sample`. Leaving range is a sample with `in_range = 0`;
  there is no separate depart call. Each call rejects a
  Node of the other profile with `AOAHID_ERR_PARAM`. The C++ forwarders and the
  three bindings follow.
- The Keyboard and Gamepad calls take the profile's name too: `aoahid_kbd` is
  `aoahid_keyboard_key` and `aoahid_dpad` is `aoahid_gamepad_dpad` (C++
  `aoahid::keyboard_key` and `aoahid::gamepad_dpad`; C# `KeyboardKey` and
  `GamepadDpad`). Every send call is now `aoahid_<profile>_<operation>`.
- The Toggle call is `aoahid_toggle_usage` (C++ `aoahid::toggle_usage`, C#
  `ToggleUsage`), so every send call follows `aoahid_<profile>_<operation>`. It
  works like `aoahid_keyboard_key`: any number of allowed Usages can be pressed
  at once, each is released by its own Usage, and a Usage that is not in the
  allow-list (including 0) is `AOAHID_ERR_PARAM`. It used to hold one Usage at
  a time, and 0 released whichever Usage was pressed.
- `aoahid_touch_contact` is no longer public: no public function takes it. The
  library keeps it internally, and the Python, C#, and Rust bindings and the
  examples no longer declare it.
- Names that did not follow the rest of the API are aligned, with no change in
  behavior: the gamepad axis call is `aoahid_gamepad_axis` (it was
  `aoahid_gamepad_set_axis`) and its Spec entry type is
  `aoahid_gamepad_axis_option` (it was `aoahid_gamepad_axis`, which the call
  could not share), like `aoahid_report_id_option`; the touch options field is
  `contact_id` (it was `contact_identifier`), as in the send calls;
  `aoahid_capability_manifest.descriptor_bytes` is `descriptor_length`, as in
  `aoahid_raw_options`; the two Linux parser limits in bits are
  `linux_hid_report_data_policy_bits` and `linux_hid_report_size_policy_bits`,
  with the unit last like the other `*_policy_bytes` fields. The Python, C#,
  and Rust bindings follow (C# `GamepadAxis`, `GamepadAxisOption`,
  `ContactId`, `DescriptorLength`, `LinuxHidReportDataPolicyBits`,
  `LinuxHidReportSizePolicyBits`).
- A Report ID has one shape everywhere: `aoahid_raw_report` and
  `aoahid_report_capability` hold an `aoahid_report_id_option report_id` (its
  `enabled` and `value`) and `wire_length`, like the options of every profile.
  They had `report_id`, `has_report_id`, and `reserved16`. The two structures
  are 12 bytes now, not 8. The raw report table and the options share one
  check of the ID (`aoa::hid::report_id_option_violation`); a table entry with
  a nonzero reserved byte is `AOAHID_ERR_PARAM` as before, and the unit tests
  now cover each rule.
- The button calls name their flag `down`, like the other send calls, and
  `aoahid_node_submit_blocking` names its wait `timeout_ms` (it was
  `deadline_ms`). Only parameter names changed.

### Added

- `aoahid_digitizer_options`, `aoahid_spec_create_digitizer`,
  `aoahid::spec_create_digitizer`, `AOAHID_PROFILE_DIGITIZER`, and the matching
  Python, C#, and Rust binding entries; the example
  `examples/c/profiles/digitizer.c`.

### Changed

- On Linux and other non-Apple Unix targets the shared library links the C++
  runtime statically (`-static-libstdc++ -static-libgcc`) and exports no
  runtime symbols, so it no longer depends on `libstdc++` or `libgcc_s`.
