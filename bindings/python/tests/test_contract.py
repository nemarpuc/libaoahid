# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libaoahid contributors
"""What the Python package promises: the raw layer (aoahid.native) chooses no value; the values of the
C initializers are in aoahid.initial and nowhere else.

The layout against the compiled C is test_layout.py (generated); initial against the C is test_initial.py.
"""

import ctypes as c

import aoahid
from aoahid import native
from aoahid.native import ContextOptions, DeviceOptions, KeyboardOptions


def test_struct_size_is_the_only_field_the_binding_sets():
    options = KeyboardOptions()
    assert options.struct_size == c.sizeof(KeyboardOptions)
    assert options.reserved == 0
    assert KeyboardOptions(struct_size=7).struct_size == 7


def test_context_options_exposes_explicit_choice_fields():
    names = {name for name, *_ in ContextOptions._fields_}
    assert {"struct_size", "event_mode", "log_level", "log_sink"} <= names


def test_device_options_exposes_target_parser_policies():
    names = {name for name, *_ in DeviceOptions._fields_}
    assert "linux_hid" in names
    policy = {name for name, *_ in native.LinuxHidPolicy._fields_}
    assert policy == {
        "descriptor_bytes",
        "fields_per_report",
        "global_stack_depth",
        "report_size_bits",
        "usages",
        "report_data_bits",
    }


def test_raw_structures_carry_no_value_but_struct_size():
    for name, cls in vars(native).items():
        if isinstance(cls, type) and issubclass(cls, c.Structure) and hasattr(cls, "struct_size"):
            raw = bytes(cls())
            assert raw[:4] == c.sizeof(cls).to_bytes(4, "little")
            assert not any(raw[4:]), name


def test_package_exports_only_the_binding_surface():
    assert "load" in aoahid.__all__
    assert "AOAHID_OK" in aoahid.__all__
    assert {"Session", "initial"} <= set(aoahid.__all__)
    assert not hasattr(aoahid, "c")
    assert not hasattr(aoahid, "os")
    assert not hasattr(aoahid, "Union")


if __name__ == "__main__":
    test_struct_size_is_the_only_field_the_binding_sets()
    test_raw_structures_carry_no_value_but_struct_size()
    test_context_options_exposes_explicit_choice_fields()
    test_device_options_exposes_target_parser_policies()
    test_package_exports_only_the_binding_surface()
