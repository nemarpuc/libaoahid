#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libaoahid contributors
"""Fail when a language binding or the layout oracle drifts from aoahid.h."""

from __future__ import annotations

import ast
import ctypes as c
import re
import runpy
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def names(pattern: str, text: str, flags: int = 0) -> set[str]:
    return set(re.findall(pattern, text, flags))


def require_equal(label: str, expected: set[str], actual: set[str]) -> None:
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    if missing or extra:
        if missing:
            print(f"{label}: missing: {', '.join(missing)}", file=sys.stderr)
        if extra:
            print(f"{label}: extra: {', '.join(extra)}", file=sys.stderr)
        raise SystemExit(1)


def integer_pairs(pattern: str, text: str, flags: int = 0) -> dict[str, int]:
    return {name: int(value) for name, value in re.findall(pattern, text, flags)}


def require_values(label: str, expected: dict[str, int], actual: dict[str, int]) -> None:
    require_equal(label, set(expected), set(actual))
    wrong = sorted(name for name in expected if expected[name] != actual[name])
    if wrong:
        print(f"{label}: wrong values: {', '.join(wrong)}", file=sys.stderr)
        raise SystemExit(1)


def require_pattern(label: str, pattern: str, text: str) -> None:
    if re.search(pattern, text, re.MULTILINE) is None:
        print(f"{label}: required metadata or ABI declaration is missing", file=sys.stderr)
        raise SystemExit(1)


def split_parameters(parameters: str) -> list[str]:
    """Split a flat ABI parameter list without splitting nested generic/call syntax."""
    values: list[str] = []
    start = 0
    depth = 0
    for index, character in enumerate(parameters):
        if character in "([<":
            depth += 1
        elif character in ")]>":
            depth -= 1
        elif character == "," and depth == 0:
            values.append(parameters[start:index].strip())
            start = index + 1
    tail = parameters[start:].strip()
    if tail and tail != "void":
        values.append(tail)
    return values


def normalize_c_type(value: str) -> str:
    value = " ".join(value.split())
    return re.sub(r"\s*\*\s*", "*", value)


def declaration_type(declaration: str) -> str:
    value = normalize_c_type(declaration)
    match = re.search(r"\b[A-Za-z_][A-Za-z0-9_]*(\[[0-9]+\])?$", value)
    if match is None:
        raise ValueError(f"cannot find ABI parameter name in {declaration!r}")
    suffix = match.group(1) or ""
    return normalize_c_type(value[: match.start()] + suffix)


def exported_signatures(text: str) -> dict[str, tuple[str, list[str]]]:
    start = text.find("/* aoahid_last_error")
    if start < 0:
        raise ValueError("public declaration block is missing aoahid_last_error")
    declarations = re.sub(r"/\*.*?\*/", "", text[start:], flags=re.DOTALL)
    result: dict[str, tuple[str, list[str]]] = {}
    for return_type, name, arguments in re.findall(
        r"AOAHID_API\s+(.*?)\bAOAHID_CALL\s+(aoahid_[a-z0-9_]+)\s*\((.*?)\)\s*;",
        declarations,
        re.DOTALL,
    ):
        parameters = split_parameters(arguments)
        result[name] = (
            normalize_c_type(return_type),
            [declaration_type(value) for value in parameters],
        )
    return result


def struct_fields(text: str) -> dict[str, list[tuple[str, str]]]:
    source = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    result: dict[str, list[tuple[str, str]]] = {}
    for name, body, closing_name in re.findall(
        r"typedef\s+struct\s+(aoahid_[a-z0-9_]+)\s*\{(.*?)\}\s*(aoahid_[a-z0-9_]+)\s*;",
        source,
        re.DOTALL,
    ):
        if name != closing_name:
            raise ValueError(f"structure tag/typedef mismatch: {name}, {closing_name}")
        fields: list[tuple[str, str]] = []
        for declaration in body.split(";"):
            declaration = declaration.strip()
            if not declaration:
                continue
            match = re.search(r"\b([A-Za-z_][A-Za-z0-9_]*)(\[[0-9]+\])?$", declaration)
            if match is None:
                raise ValueError(f"cannot parse {name} field {declaration!r}")
            fields.append((match.group(1), declaration_type(declaration)))
        result[name] = fields
    return result


def rust_type(c_type: str) -> str:
    base_types = {
        "aoahid_result": "aoahid_result",
        "uint8_t": "u8",
        "uint16_t": "u16",
        "uint32_t": "u32",
        "uint64_t": "u64",
        "int32_t": "i32",
        "size_t": "usize",
        "char": "c_char",
        "void": "c_void",
    }
    value = normalize_c_type(c_type)
    array = re.fullmatch(r"(.+)\[([0-9]+)\]", value)
    if array is not None:
        return f"[{rust_type(array.group(1))}; {array.group(2)}]"
    parts = value.split("*")
    base = parts[0].removeprefix("const ").strip()
    mapped = base_types.get(base, base)
    if len(parts) == 1:
        return "()" if base == "void" else mapped
    pointee_is_const = parts[0].startswith("const ")
    for pointer_qualifiers in parts[1:]:
        mapped = ("*const " if pointee_is_const else "*mut ") + mapped
        pointee_is_const = "const" in pointer_qualifiers.split()
    return mapped


def compact_type(value: str) -> str:
    return re.sub(r"\s+", "", value)


def python_type(
    c_type: str,
    namespace: dict[str, object],
    enum_aliases: set[str],
) -> object:
    value = normalize_c_type(c_type)
    array = re.fullmatch(r"(.+)\[([0-9]+)\]", value)
    if array is not None:
        return python_type(array.group(1), namespace, enum_aliases) * int(array.group(2))
    without_const = re.sub(r"\bconst\b", "", value)
    without_const = normalize_c_type(without_const)
    parts = without_const.split("*")
    base = parts[0]
    base_types: dict[str, object] = {
        "uint8_t": c.c_uint8,
        "uint16_t": c.c_uint16,
        "uint32_t": c.c_uint32,
        "uint64_t": c.c_uint64,
        "int32_t": c.c_int32,
        "size_t": c.c_size_t,
        "char": c.c_char,
        "void": None,
        "aoahid_log_sink": namespace["LogSink"],
    }
    if base in enum_aliases:
        current: object = c.c_int32
    elif base in base_types:
        current = base_types[base]
    elif base in namespace["ABI_STRUCTS"]:
        current = namespace["ABI_STRUCTS"][base]
    elif base.startswith("aoahid_"):
        python_name = "".join(part.capitalize() for part in base.removeprefix("aoahid_").split("_"))
        current = namespace[python_name]
    else:
        raise ValueError(f"no ctypes mapping for {c_type!r}")
    for pointer_index in range(len(parts) - 1):
        if pointer_index == 0 and base == "char":
            current = c.c_char_p
        elif pointer_index == 0 and base == "void":
            current = c.c_void_p
        else:
            current = c.POINTER(current)
    return current


CSHARP_ENUM_TYPES = {
    "aoahid_result": "Result",
    "aoahid_event_mode": "EventMode",
    "aoahid_channel_read_mode": "ChannelReadMode",
    "aoahid_interface_claim_policy": "ClaimPolicy",
    "aoahid_log_level": "LogLevel",
    "aoahid_profile_kind": "ProfileKind",
    "aoahid_android_status": "AndroidStatus",
    "aoahid_axis_role": "AxisRole",
    "aoahid_dpad_representation": "DpadRepresentation",
    "aoahid_usage_semantic": "UsageSemantic",
}

CSHARP_ENUM_CONSTANT_PREFIXES = {
    "aoahid_event_mode": "AOAHID_EVENT_",
    "aoahid_channel_read_mode": "AOAHID_CHANNEL_READ_",
    "aoahid_interface_claim_policy": "AOAHID_INTERFACE_CLAIM_",
    "aoahid_log_level": "AOAHID_LOG_",
    "aoahid_profile_kind": "AOAHID_PROFILE_",
    "aoahid_android_status": "AOAHID_ANDROID_",
    "aoahid_axis_role": "AOAHID_AXIS_",
    "aoahid_dpad_representation": "AOAHID_DPAD_",
    "aoahid_usage_semantic": "AOAHID_USAGE_",
}


def pascal_case_constant(value: str) -> str:
    return "".join(part.title() for part in value.split("_"))


def csharp_enum_member(alias: str, constant: str) -> str:
    if alias == "aoahid_result":
        value = constant.removeprefix("AOAHID_")
        if value.startswith("ERR_"):
            value = value.removeprefix("ERR_")
        if value == "PARAM":
            return "Parameter"
        return pascal_case_constant(value)
    prefix = CSHARP_ENUM_CONSTANT_PREFIXES[alias]
    if not constant.startswith(prefix):
        raise ValueError(f"{constant} does not have the prefix for {alias}")
    return pascal_case_constant(constant.removeprefix(prefix))


def csharp_field_name(value: str) -> str:
    if value == "ScanTimeUnit100us":
        return "scan_time_unit_100us"
    result: list[str] = []
    for index, character in enumerate(value):
        if character.isupper() and index != 0:
            previous = value[index - 1]
            next_is_lower = index + 1 < len(value) and value[index + 1].islower()
            if previous.islower() or previous.isdigit() or next_is_lower:
                result.append("_")
        result.append(character.lower())
    return "".join(result)


def csharp_type(
    c_type: str,
    role: str,
    structure_names: dict[str, str],
) -> str:
    value = normalize_c_type(c_type)
    array = re.fullmatch(r"(.+)\[([0-9]+)\]", value)
    if array is not None:
        element = csharp_type(array.group(1), "scalar", structure_names)
        return f"fixed {element}[{array.group(2)}]"
    parts = value.split("*")
    base = parts[0].removeprefix("const ").strip()
    stars = len(parts) - 1
    primitives = {
        "uint8_t": "byte",
        "uint16_t": "ushort",
        "uint32_t": "uint",
        "uint64_t": "ulong",
        "int32_t": "int",
        "size_t": "nuint",
        "void": "void",
    }
    mapped = CSHARP_ENUM_TYPES.get(base, primitives.get(base, structure_names.get(base, base)))
    if stars == 0:
        return "nint" if base == "aoahid_log_sink" else mapped
    if role in ("field", "return"):
        return "nint"
    if stars == 2:
        return "out nint"
    if base == "size_t":
        return "out nuint"
    if base in structure_names and base not in {
        "aoahid_context",
        "aoahid_discovery",
        "aoahid_device",
        "aoahid_spec",
        "aoahid_node",
    }:
        return ("in " if parts[0].startswith("const ") else "ref ") + mapped
    return "nint"


def require_signatures(
    label: str,
    expected: dict[str, tuple[str, list[str]]],
    actual: dict[str, tuple[str, list[str]]],
) -> None:
    require_equal(label, set(expected), set(actual))
    wrong = sorted(name for name in expected if expected[name] != actual[name])
    if wrong:
        print(f"{label}: wrong signatures: {', '.join(wrong)}", file=sys.stderr)
        raise SystemExit(1)


def require_arities(label: str, expected: dict[str, int], actual: dict[str, int]) -> None:
    require_equal(label, set(expected), set(actual))
    wrong = sorted(name for name, count in expected.items() if actual[name] != count)
    if wrong:
        rendered = ", ".join(
            f"{name} (C {expected[name]}, binding {actual[name]})" for name in wrong
        )
        print(f"{label}: wrong parameter counts: {rendered}", file=sys.stderr)
        raise SystemExit(1)


header = (ROOT / "include/aoahid.h").read_text(encoding="utf-8")
python = (ROOT / "bindings/python/aoahid/native.py").read_text(encoding="utf-8")
csharp = (ROOT / "bindings/csharp/AoaHid/Native.cs").read_text(encoding="utf-8")
rust = (ROOT / "bindings/rust/aoahid-sys/src/lib.rs").read_text(encoding="utf-8")
oracle = (ROOT / "tests/abi/layout_oracle.c").read_text(encoding="utf-8")
python_project = (ROOT / "bindings/python/pyproject.toml").read_text(encoding="utf-8")
rust_sys_project = (ROOT / "bindings/rust/aoahid-sys/Cargo.toml").read_text(encoding="utf-8")
rust_project = (ROOT / "bindings/rust/aoahid/Cargo.toml").read_text(encoding="utf-8")
csharp_project = (ROOT / "bindings/csharp/AoaHid/AoaHid.csproj").read_text(encoding="utf-8")

functions = names(r"\bAOAHID_CALL\s+(aoahid_[a-z0-9_]+)\s*\(", header)
python_functions = names(r'declare\("(aoahid_[a-z0-9_]+)"', python)
python_functions |= names(r'^\s*"(aoahid_spec_create_[a-z0-9_]+)"\s*:', python, re.MULTILINE)
require_equal("Python functions", functions, python_functions)
require_equal("C# functions", functions, names(r'EntryPoint\s*=\s*"(aoahid_[a-z0-9_]+)"', csharp))
require_equal("Rust functions", functions, names(r"\bpub fn (aoahid_[a-z0-9_]+)\s*\(", rust))

c_signatures = exported_signatures(header)
c_arities = {name: len(signature[1]) for name, signature in c_signatures.items()}
rust_arities = {
    name: len(split_parameters(arguments))
    for name, arguments in re.findall(
        r"\bpub fn\s+(aoahid_[a-z0-9_]+)\s*\((.*?)\)\s*(?:->\s*[^;]+)?;",
        rust,
        re.DOTALL,
    )
}
csharp_arities = {
    name: len(split_parameters(arguments))
    for name, arguments in re.findall(
        r'EntryPoint\s*=\s*"(aoahid_[a-z0-9_]+)"[^\]]*\]\s*'
        r"public\s+static\s+extern\s+[^;()]+\s+\w+\s*\((.*?)\)\s*;",
        csharp,
        re.DOTALL,
    )
}
python_arities: dict[str, int] = {}
for node in ast.walk(ast.parse(python)):
    if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Name):
        continue
    if node.func.id != "declare" or len(node.args) < 2:
        continue
    if isinstance(node.args[0], ast.Constant) and isinstance(node.args[0].value, str):
        if isinstance(node.args[1], (ast.List, ast.Tuple)):
            python_arities[node.args[0].value] = len(node.args[1].elts)
for name in names(r'^\s*"(aoahid_spec_create_[a-z0-9_]+)"\s*:', python, re.MULTILINE):
    python_arities[name] = 2
require_arities("Python function arities", c_arities, python_arities)
require_arities("C# function arities", c_arities, csharp_arities)
require_arities("Rust function arities", c_arities, rust_arities)

expected_rust_signatures = {
    name: (
        compact_type(rust_type(return_type)),
        [compact_type(rust_type(value)) for value in parameters],
    )
    for name, (return_type, parameters) in c_signatures.items()
}
actual_rust_signatures: dict[str, tuple[str, list[str]]] = {}
for name, arguments, return_type in re.findall(
    r"\bpub fn\s+(aoahid_[a-z0-9_]+)\s*\((.*?)\)\s*(?:->\s*([^;]+))?;",
    rust,
    re.DOTALL,
):
    parameter_types = [value.split(":", 1)[1].strip() for value in split_parameters(arguments)]
    actual_rust_signatures[name] = (
        compact_type(return_type or "()"),
        [compact_type(value) for value in parameter_types],
    )
require_signatures("Rust function types", expected_rust_signatures, actual_rust_signatures)

c_struct_fields = struct_fields(header)
rust_struct_fields: dict[str, list[tuple[str, str]]] = {}
for name, body in re.findall(
    r"c_struct!\((aoahid_[a-z0-9_]+)\s*\{(.*?)\}\s*\);",
    rust,
    re.DOTALL,
):
    fields = []
    for declaration in split_parameters(body):
        field_name, field_type = declaration.split(":", 1)
        fields.append((field_name.strip(), compact_type(field_type)))
    rust_struct_fields[name] = fields
expected_rust_struct_fields = {
    name: [(field, compact_type(rust_type(field_type))) for field, field_type in fields]
    for name, fields in c_struct_fields.items()
}
if expected_rust_struct_fields != rust_struct_fields:
    wrong = sorted(
        name
        for name in expected_rust_struct_fields
        if expected_rust_struct_fields.get(name) != rust_struct_fields.get(name)
    )
    wrong.extend(sorted(set(rust_struct_fields) - set(expected_rust_struct_fields)))
    print(f"Rust structure field types: mismatch in {', '.join(wrong)}", file=sys.stderr)
    raise SystemExit(1)

python_namespace = runpy.run_path(str(ROOT / "bindings/python/aoahid/native.py"))


class FakeFunction:
    pass


class FakeLibrary:
    def __init__(self) -> None:
        self.functions: dict[str, FakeFunction] = {}

    def __getattr__(self, name: str) -> FakeFunction:
        return self.functions.setdefault(name, FakeFunction())


fake_library = FakeLibrary()
original_cdll = c.CDLL
c.CDLL = lambda _path: fake_library  # type: ignore[assignment]
try:
    python_namespace["load"]("binding-check-placeholder")
finally:
    c.CDLL = original_cdll  # type: ignore[assignment]

enum_aliases = names(r"typedef\s+int32_t\s+(aoahid_[a-z0-9_]+)\s*;", header)
for name, (return_type, parameters) in c_signatures.items():
    function = fake_library.functions[name]
    expected_result = python_type(return_type, python_namespace, enum_aliases)
    expected_arguments = [python_type(value, python_namespace, enum_aliases) for value in parameters]
    if function.restype != expected_result or function.argtypes != expected_arguments:
        print(f"Python function types: mismatch in {name}", file=sys.stderr)
        raise SystemExit(1)
for name, fields in c_struct_fields.items():
    structure = python_namespace["ABI_STRUCTS"][name]
    actual_fields = [(field[0], field[1]) for field in structure._fields_]
    expected_fields = [
        (field, python_type(field_type, python_namespace, enum_aliases))
        for field, field_type in fields
    ]
    if actual_fields != expected_fields:
        print(f"Python structure field types: mismatch in {name}", file=sys.stderr)
        raise SystemExit(1)

csharp_structure_names = {
    c_name: managed_name
    for c_name, managed_name in re.findall(
        r'^\s*\["(aoahid_[a-z0-9_]+)"\]\s*=\s*typeof\((\w+)\)',
        csharp,
        re.MULTILINE,
    )
}
expected_csharp_signatures = {
    name: (
        csharp_type(return_type, "return", csharp_structure_names),
        [csharp_type(value, "parameter", csharp_structure_names) for value in parameters],
    )
    for name, (return_type, parameters) in c_signatures.items()
}
actual_csharp_signatures: dict[str, tuple[str, list[str]]] = {}
for name, return_type, arguments in re.findall(
    r'EntryPoint\s*=\s*"(aoahid_[a-z0-9_]+)"[^\]]*\]\s*'
    r"public\s+static\s+extern\s+(\w+)\s+\w+\s*\((.*?)\)\s*;",
    csharp,
    re.DOTALL,
):
    parameter_types = []
    for parameter in split_parameters(arguments):
        parameter_type, separator, _parameter_name = parameter.rpartition(" ")
        if not separator:
            raise ValueError(f"cannot parse C# parameter {parameter!r}")
        parameter_types.append(parameter_type.strip())
    actual_csharp_signatures[name] = (return_type, parameter_types)
require_signatures("C# function types", expected_csharp_signatures, actual_csharp_signatures)

csharp_struct_bodies = {
    name: body
    for name, body in re.findall(
        r"\[StructLayout\(LayoutKind\.Sequential\)\]\s*"
        r"public\s+(?:unsafe\s+)?struct\s+(\w+)\s*\{(.*?)\n\}",
        csharp,
        re.DOTALL,
    )
}
for c_name, fields in c_struct_fields.items():
    managed_name = csharp_structure_names[c_name]
    body = csharp_struct_bodies[managed_name]
    actual_fields: list[tuple[str, str]] = []
    for match in re.finditer(
        r"^\s*public\s+(?:(fixed)\s+)?(\w+)\s+\w+(?:\[([0-9]+)\])?\s*;",
        body,
        re.MULTILINE,
    ):
        prefix, field_type, count = match.groups()
        declaration = match.group(0)
        field_name_match = re.search(r"\b(\w+)(?:\[[0-9]+\])?\s*;\s*$", declaration)
        if field_name_match is None:
            raise ValueError(f"cannot parse C# field {declaration!r}")
        actual_fields.append(
            (
                csharp_field_name(field_name_match.group(1)),
                f"fixed {field_type}[{count}]" if prefix else field_type,
            )
        )
    expected_fields = [
        (field, csharp_type(field_type, "field", csharp_structure_names))
        for field, field_type in fields
    ]
    if actual_fields != expected_fields:
        print(f"C# structure fields: mismatch in {c_name}", file=sys.stderr)
        raise SystemExit(1)

structures = names(r"typedef struct (aoahid_[a-z0-9_]+)\s*\{", header)
python_struct_map = python.split("ABI_STRUCTS = {", 1)[1].split("}\n", 1)[0]
require_equal("C layout oracle structures", structures, names(r"AOAHID_BEGIN\((aoahid_[a-z0-9_]+)\)", oracle))
require_equal("Python structures", structures, names(r'^\s*"(aoahid_[a-z0-9_]+)":', python_struct_map, re.MULTILINE))
require_equal("C# structures", structures, names(r'^\s*\["(aoahid_[a-z0-9_]+)"\]\s*=', csharp, re.MULTILINE))
require_equal("Rust structures", structures, names(r"c_struct!\((aoahid_[a-z0-9_]+)\s*\{", rust))

constants = integer_pairs(r"\b(AOAHID_[A-Z0-9_]+)\s*=\s*(-?[0-9]+)", header)
python_constant_values = integer_pairs(
    r"^(AOAHID_[A-Z0-9_]+)\s*=\s*(-?[0-9]+)", python, re.MULTILINE
)
rust_constant_values = integer_pairs(
    r"^pub const (AOAHID_[A-Z0-9_]+):\s*i32\s*=\s*(-?[0-9]+);", rust, re.MULTILINE
)
require_values(
    "Python enum constants",
    constants,
    {name: python_constant_values[name] for name in constants if name in python_constant_values},
)
require_values(
    "C# enum constants",
    constants,
    integer_pairs(r'^\s*\["(AOAHID_[A-Z0-9_]+)"\]\s*=\s*(-?[0-9]+),', csharp, re.MULTILINE),
)

header_without_comments = re.sub(r"/\*.*?\*/", "", header, flags=re.DOTALL)
c_enum_domains = {
    alias: [(name, int(value)) for name, value in re.findall(
        r"(AOAHID_[A-Z0-9_]+)\s*=\s*(-?[0-9]+)", body
    )]
    for alias, body in re.findall(
        r"typedef\s+int32_t\s+(aoahid_[a-z0-9_]+)\s*;\s*enum\s*\{(.*?)\}\s*;",
        header_without_comments,
        re.DOTALL,
    )
}
csharp_enum_bodies = {
    name: body
    for name, body in re.findall(
        r"public\s+enum\s+(\w+)\s*:\s*int\s*\{(.*?)\}", csharp, re.DOTALL
    )
}
require_equal("C# enum types", set(CSHARP_ENUM_TYPES.values()), set(csharp_enum_bodies))
for alias, entries in c_enum_domains.items():
    managed_name = CSHARP_ENUM_TYPES[alias]
    actual_entries = [
        (name, int(value))
        for name, value in re.findall(r"\b(\w+)\s*=\s*(-?[0-9]+)", csharp_enum_bodies[managed_name])
    ]
    expected_entries = [
        (csharp_enum_member(alias, constant), value) for constant, value in entries
    ]
    if actual_entries != expected_entries:
        print(f"C# enum members: mismatch in {managed_name}", file=sys.stderr)
        raise SystemExit(1)
require_values(
    "Rust enum constants",
    constants,
    {name: rust_constant_values[name] for name in constants if name in rust_constant_values},
)

versions = integer_pairs(r"^#define (AOAHID_VERSION_[A-Z]+)\s+([0-9]+)$", header, re.MULTILINE)
require_values(
    "Python version constants",
    versions,
    {name: python_constant_values[name] for name in versions if name in python_constant_values},
)
require_values(
    "C# version constants",
    versions,
    integer_pairs(r"^\s*public const uint (AOAHID_VERSION_[A-Z]+)\s*=\s*([0-9]+);", csharp, re.MULTILINE),
)
rust_versions = integer_pairs(
    r"^pub const (AOAHID_VERSION_[A-Z]+):\s*u32\s*=\s*([0-9]+);", rust, re.MULTILINE
)
require_values("Rust version constants", versions, rust_versions)

if csharp.count("CallingConvention = Call") != len(functions):
    print("C# calling conventions: every DllImport must select Call", file=sys.stderr)
    raise SystemExit(1)
require_pattern(
    "C# cdecl calling convention",
    r"private\s+const\s+CallingConvention\s+Call\s*=\s*CallingConvention\.Cdecl\s*;",
    csharp,
)
require_pattern("Python cdecl loader", r"library\s*=\s*c\.CDLL\(", python)
require_pattern("Rust C calling convention", r'extern\s+"C"\s*\{', rust)
require_pattern("Rust callback C calling convention", r'unsafe\s+extern\s+"C"\s+fn', rust)

package_version = ".".join(
    str(versions[name])
    for name in ("AOAHID_VERSION_MAJOR", "AOAHID_VERSION_MINOR", "AOAHID_VERSION_PATCH")
)
repository = "https://github.com/nemarpuc/libaoahid"
package_checks = (
    ("Python name", r'^name\s*=\s*"aoahid"\s*$', python_project),
    ("Python version", rf'^version\s*=\s*"{re.escape(package_version)}"\s*$', python_project),
    ("Python homepage", rf'^Homepage\s*=\s*"{re.escape(repository)}"\s*$', python_project),
    ("Python repository", rf'^Repository\s*=\s*"{re.escape(repository)}"\s*$', python_project),
    (
        "Python issue tracker",
        rf'^Issues\s*=\s*"{re.escape(repository)}/issues"\s*$',
        python_project,
    ),
    ("Rust sys name", r'^name\s*=\s*"aoahid-sys"\s*$', rust_sys_project),
    ("Rust wrapper name", r'^name\s*=\s*"aoahid"\s*$', rust_project),
    (
        "Rust sys version",
        rf'^version\s*=\s*"{re.escape(package_version)}"\s*$',
        rust_sys_project,
    ),
    (
        "Rust wrapper version",
        rf'^version\s*=\s*"{re.escape(package_version)}"\s*$',
        rust_project,
    ),
    ("Rust sys homepage", rf'^homepage\s*=\s*"{re.escape(repository)}"\s*$', rust_sys_project),
    ("Rust sys repository", rf'^repository\s*=\s*"{re.escape(repository)}"\s*$', rust_sys_project),
    ("Rust wrapper homepage", rf'^homepage\s*=\s*"{re.escape(repository)}"\s*$', rust_project),
    ("Rust wrapper repository", rf'^repository\s*=\s*"{re.escape(repository)}"\s*$', rust_project),
    ("NuGet package ID", r"<PackageId>AoaHid</PackageId>", csharp_project),
    ("NuGet version", rf"<Version>{re.escape(package_version)}</Version>", csharp_project),
    ("NuGet project URL", rf"<PackageProjectUrl>{re.escape(repository)}</PackageProjectUrl>", csharp_project),
    ("NuGet repository URL", rf"<RepositoryUrl>{re.escape(repository)}</RepositoryUrl>", csharp_project),
    ("NuGet repository type", r"<RepositoryType>git</RepositoryType>", csharp_project),
)
for label, pattern, package_text in package_checks:
    require_pattern(label, pattern, package_text)

print(f"bindings match aoahid.h: {len(functions)} functions, {len(structures)} structures, {len(constants)} constants")
