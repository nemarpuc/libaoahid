# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libaoahid contributors
"""Check literal ctypes layout and reject binding-supplied option defaults."""

import ctypes as c
import os
import subprocess

import aoahid
from aoahid import native
from aoahid.native import ABI_STRUCTS, ContextOptions, DeviceOptions, KeyboardOptions


def test_struct_size_is_not_filled_by_binding():
    options = KeyboardOptions()
    assert options.struct_size == 0
    options.struct_size = c.sizeof(options)
    assert options.struct_size == c.sizeof(KeyboardOptions)


def test_context_options_exposes_explicit_choice_fields():
    names = {name for name, *_ in ContextOptions._fields_}
    assert {"struct_size", "event_mode", "log_level", "log_sink"} <= names


def test_device_options_exposes_target_parser_policies():
    names = {name for name, *_ in DeviceOptions._fields_}
    assert {
        "linux_hid_fields_per_report_policy",
        "linux_hid_global_stack_depth_policy",
        "linux_hid_usages_policy",
        "linux_hid_report_data_policy_bits",
        "linux_hid_report_size_policy_bits",
    } <= names


def test_package_exports_only_the_binding_surface():
    assert "load" in aoahid.__all__
    assert "AOAHID_OK" in aoahid.__all__
    assert not hasattr(aoahid, "c")
    assert not hasattr(aoahid, "os")
    assert not hasattr(aoahid, "Union")


def _c_oracle_values(executable: str) -> dict[str, int]:
    output = subprocess.check_output([executable], text=True)
    return {
        name: int(value)
        for line in output.splitlines()
        for name, value in [line.split("=", 1)]
    }


def verify_compiled_c_layout(executable: str) -> None:
    """Compare every binding size, offset, and constant with compiled C."""
    oracle = _c_oracle_values(executable)
    layout_oracle = {
        name: value for name, value in oracle.items() if name.startswith("aoahid_")
    }
    constant_oracle = {
        name: value for name, value in oracle.items() if name.startswith("AOAHID_")
    }
    expected_keys: set[str] = set()
    for c_name, structure in ABI_STRUCTS.items():
        size_key = f"{c_name}.size"
        expected_keys.add(size_key)
        assert c.sizeof(structure) == layout_oracle[size_key]
        for field_name, *_ in structure._fields_:
            key = f"{c_name}.{field_name}"
            expected_keys.add(key)
            assert getattr(structure, field_name).offset == layout_oracle[key]
    assert set(layout_oracle) == expected_keys

    binding_constants = {
        name: getattr(native, name)
        for name in native.__all__
        if name.startswith("AOAHID_")
    }
    assert binding_constants == constant_oracle


def test_all_layouts_match_compiled_c_oracle():
    oracle = os.environ.get("AOAHID_ABI_ORACLE")
    if oracle is None:
        return
    verify_compiled_c_layout(oracle)


if __name__ == "__main__":
    test_struct_size_is_not_filled_by_binding()
    test_context_options_exposes_explicit_choice_fields()
    test_device_options_exposes_target_parser_policies()
    test_package_exports_only_the_binding_surface()
    oracle_path = os.environ.get("AOAHID_ABI_ORACLE")
    if oracle_path is None:
        raise SystemExit("AOAHID_ABI_ORACLE is required for the executable check")
    verify_compiled_c_layout(oracle_path)
