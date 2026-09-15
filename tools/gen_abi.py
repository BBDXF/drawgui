#!/usr/bin/env python3
"""Generate drawgui's C ABI: the header, the exception-wrapping trampolines,
and the TypeScript ambient declarations.

abi/drawgui.def.toml is the single source of truth (design.md section 5.8
decision 5). This script turns it into:

  include/drawgui/abi/drawgui.h        a pure C header - no C++, no Skia
  src/abi/drawgui_abi.generated.cpp    extern "C" trampolines, each wrapping
                                       its call in the IDENTICAL try/catch
                                       shape (design.md section 5.17.1)
  abi/drawgui.d.ts                     TypeScript ambient declarations (P6's
                                       eventual FFI binding reads this; no
                                       code in this project consumes it yet)

`prop_id` constants are re-emitted here in C-compatible `#define` form from
props/drawgui.props.toml's own validated Definitions - imported directly from
tools/gen_props.py rather than re-parsed, so there is exactly one parser and
one set of validation rules for that table. Nothing about themes/schema.toml
is re-emitted here: this slice declines the theme ABI entirely (see
abi/drawgui.def.toml's own header comment and doc/abi.md section 6), so a
token_id constant with no ABI consumer would be exactly the kind of
speculative surface design.md section 5.8's "只导出必要面" forbids.

Usage:
  python3 tools/gen_abi.py            regenerate all three outputs
  python3 tools/gen_abi.py --check    exit non-zero if any output is stale
"""

from __future__ import annotations

import argparse
import re
import sys
import tomllib
from dataclasses import dataclass, field as dataclass_field
from pathlib import Path

TOOL_NAME = "tools/gen_abi.py"
SOURCE_NAME = "abi/drawgui.def.toml"

TOML_RELPATH = Path("abi") / "drawgui.def.toml"
HEADER_RELPATH = Path("include") / "drawgui" / "abi" / "drawgui.h"
IMPL_RELPATH = Path("src") / "abi" / "drawgui_abi.generated.cpp"
DTS_RELPATH = Path("abi") / "drawgui.d.ts"

SUPPORTED_SCHEMA_VERSION = 1

NAME_PATTERN = re.compile(r"^[a-z][a-z0-9_]*$")
CONST_NAME_PATTERN = re.compile(r"^[A-Z][A-Z0-9_]*$")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_props  # noqa: E402  (needs the sys.path line above)


class GenError(Exception):
    """A fatal problem with the ABI definitions or the generated files."""


# --------------------------------------------------------------------------
# Data model
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class OpaqueType:
    name: str
    summary: str


@dataclass(frozen=True)
class StructField:
    name: str
    type: str
    summary: str = ""


@dataclass(frozen=True)
class Struct:
    name: str
    summary: str
    fields: tuple[StructField, ...]


@dataclass(frozen=True)
class ConstValue:
    name: str
    id: int
    summary: str = ""


@dataclass(frozen=True)
class ConstantGroup:
    name: str
    prefix: str
    c_type: str
    summary: str
    values: tuple[ConstValue, ...]


@dataclass(frozen=True)
class Param:
    name: str
    type: str


@dataclass(frozen=True)
class Function:
    name: str
    ret: str
    impl: str
    summary: str
    params: tuple[Param, ...] = dataclass_field(default=())


@dataclass(frozen=True)
class Definitions:
    schema_version: int
    abi_major: int
    abi_minor: int
    opaque_types: tuple[OpaqueType, ...]
    structs: tuple[Struct, ...]
    constant_groups: tuple[ConstantGroup, ...]
    functions: tuple[Function, ...]
    props: gen_props.Definitions


# --------------------------------------------------------------------------
# Loading and validation
# --------------------------------------------------------------------------

C_PRIMITIVE_TYPES = frozenset(
    {
        "void",
        "int32_t",
        "uint32_t",
        "uint16_t",
        "float",
        "const char*",
    }
)


def _known_type_names(defs_opaque: list[OpaqueType], defs_structs: list[Struct]) -> set[str]:
    names = set(C_PRIMITIVE_TYPES)
    for opaque in defs_opaque:
        names.add(opaque.name + "*")
        names.add("const " + opaque.name + "*")
    for struct in defs_structs:
        names.add(struct.name)
        names.add(struct.name + "*")
        names.add("const " + struct.name + "*")
    return names


def _parse_opaque_type(entry: dict, index: int) -> OpaqueType:
    where = f"[[opaque_type]] #{index + 1}"
    name = entry.get("name")
    if not isinstance(name, str) or not NAME_PATTERN.match(name) or not name.endswith("_t"):
        raise GenError(f"{where}: 'name' must be a snake_case identifier ending in '_t'")
    summary = entry.get("summary")
    if not isinstance(summary, str) or not summary:
        raise GenError(f"opaque_type '{name}': missing 'summary'")
    return OpaqueType(name=name, summary=summary)


def _parse_struct_field(entry: dict, struct_name: str, index: int) -> StructField:
    where = f"struct '{struct_name}' field #{index + 1}"
    name = entry.get("name")
    if not isinstance(name, str) or not NAME_PATTERN.match(name):
        raise GenError(f"{where}: 'name' must be snake_case")
    field_type = entry.get("type")
    if not isinstance(field_type, str) or not field_type:
        raise GenError(f"{where}: missing 'type'")
    return StructField(name=name, type=field_type, summary=entry.get("summary", ""))


def _parse_struct(entry: dict, index: int) -> Struct:
    where = f"[[struct]] #{index + 1}"
    name = entry.get("name")
    if isinstance(name, str) and name:
        where = f"struct '{name}'"
    if not isinstance(name, str) or not NAME_PATTERN.match(name):
        raise GenError(f"{where}: 'name' must be snake_case")
    summary = entry.get("summary")
    if not isinstance(summary, str) or not summary:
        raise GenError(f"{where}: missing 'summary'")
    raw_fields = entry.get("fields")
    if not isinstance(raw_fields, list) or not raw_fields:
        raise GenError(f"{where}: 'fields' must be a non-empty array")
    fields = tuple(_parse_struct_field(f, name, i) for i, f in enumerate(raw_fields))
    if fields[0].name != "size":
        raise GenError(
            f"{where}: the first field must be 'size' (design.md section 5.8 decision 4 - "
            f"every ABI struct carries a size field so an old, shorter build is detected)"
        )
    seen = set()
    for f in fields:
        if f.name in seen:
            raise GenError(f"{where}: duplicate field name '{f.name}'")
        seen.add(f.name)
    return Struct(name=name, summary=summary, fields=fields)


def _parse_const_value(entry: dict, group_name: str, index: int) -> ConstValue:
    where = f"constant_group '{group_name}' value #{index + 1}"
    name = entry.get("name")
    if not isinstance(name, str) or not CONST_NAME_PATTERN.match(name):
        raise GenError(f"{where}: 'name' must be SCREAMING_SNAKE_CASE")
    value_id = entry.get("id")
    if not isinstance(value_id, int) or isinstance(value_id, bool):
        raise GenError(f"{where}: 'id' must be an integer")
    return ConstValue(name=name, id=value_id, summary=entry.get("summary", ""))


def _parse_constant_group(entry: dict, index: int) -> ConstantGroup:
    where = f"[[constant_group]] #{index + 1}"
    name = entry.get("name")
    if isinstance(name, str) and name:
        where = f"constant_group '{name}'"
    if not isinstance(name, str) or not NAME_PATTERN.match(name):
        raise GenError(f"{where}: 'name' must be snake_case")
    prefix = entry.get("prefix")
    if not isinstance(prefix, str) or not prefix.endswith("_") or not prefix.isupper():
        raise GenError(f"{where}: 'prefix' must be an UPPER_CASE_ prefix")
    c_type = entry.get("c_type")
    if not isinstance(c_type, str) or not c_type:
        raise GenError(f"{where}: missing 'c_type'")
    summary = entry.get("summary")
    if not isinstance(summary, str) or not summary:
        raise GenError(f"{where}: missing 'summary'")
    raw_values = entry.get("values")
    if not isinstance(raw_values, list) or not raw_values:
        raise GenError(f"{where}: 'values' must be a non-empty array")
    values = tuple(_parse_const_value(v, name, i) for i, v in enumerate(raw_values))
    names_seen: dict[str, int] = {}
    ids_seen: dict[int, str] = {}
    for v in values:
        if v.name in names_seen:
            raise GenError(f"{where}: duplicate value name '{v.name}'")
        if v.id in ids_seen:
            raise GenError(f"{where}: duplicate value id {v.id} ('{ids_seen[v.id]}' and '{v.name}')")
        names_seen[v.name] = v.id
        ids_seen[v.id] = v.name
    return ConstantGroup(name=name, prefix=prefix, c_type=c_type, summary=summary, values=values)


def _parse_param(entry: dict, fn_name: str, index: int, known_types: set[str]) -> Param:
    where = f"function '{fn_name}' param #{index + 1}"
    name = entry.get("name")
    if not isinstance(name, str) or not NAME_PATTERN.match(name):
        raise GenError(f"{where}: 'name' must be snake_case")
    param_type = entry.get("type")
    if not isinstance(param_type, str) or param_type not in known_types:
        raise GenError(f"{where}: 'type' {param_type!r} is not a known ABI type")
    return Param(name=name, type=param_type)


def _parse_function(entry: dict, index: int, known_types: set[str]) -> Function:
    where = f"[[function]] #{index + 1}"
    name = entry.get("name")
    if isinstance(name, str) and name:
        where = f"function '{name}'"
    if not isinstance(name, str) or not name.startswith("dg_") or not NAME_PATTERN.match(name):
        raise GenError(f"{where}: 'name' must be snake_case starting with 'dg_'")
    ret = entry.get("ret")
    if not isinstance(ret, str) or ret not in known_types:
        raise GenError(f"{where}: 'ret' {ret!r} is not a known ABI type")
    impl = entry.get("impl")
    if not isinstance(impl, str) or not NAME_PATTERN.match(impl):
        raise GenError(f"{where}: 'impl' must be snake_case")
    summary = entry.get("summary")
    if not isinstance(summary, str) or not summary:
        raise GenError(f"{where}: missing 'summary'")
    raw_params = entry.get("params", [])
    if not isinstance(raw_params, list):
        raise GenError(f"{where}: 'params' must be an array")
    params = tuple(_parse_param(p, name, i, known_types) for i, p in enumerate(raw_params))
    return Function(name=name, ret=ret, impl=impl, summary=summary, params=params)


def load_definitions(toml_path: Path, props_toml_path: Path) -> Definitions:
    try:
        raw = tomllib.loads(toml_path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise GenError(f"missing source of truth: {toml_path}") from exc
    except tomllib.TOMLDecodeError as exc:
        raise GenError(f"{toml_path}: invalid TOML: {exc}") from exc

    schema_version = raw.get("schema_version")
    if schema_version != SUPPORTED_SCHEMA_VERSION:
        raise GenError(
            f"{toml_path}: schema_version {schema_version!r} is not supported; "
            f"this generator understands {SUPPORTED_SCHEMA_VERSION}"
        )

    meta = raw.get("meta")
    if not isinstance(meta, dict):
        raise GenError(f"{toml_path}: missing [meta] table")
    abi_major = meta.get("abi_major")
    abi_minor = meta.get("abi_minor")
    if not isinstance(abi_major, int) or isinstance(abi_major, bool) or abi_major < 0:
        raise GenError("[meta]: 'abi_major' must be a non-negative integer")
    if not isinstance(abi_minor, int) or isinstance(abi_minor, bool) or abi_minor < 0:
        raise GenError("[meta]: 'abi_minor' must be a non-negative integer")

    opaque_types = tuple(
        _parse_opaque_type(e, i) for i, e in enumerate(raw.get("opaque_type", []))
    )
    if not opaque_types:
        raise GenError(f"{toml_path}: no [[opaque_type]] entries found")

    structs = tuple(_parse_struct(e, i) for i, e in enumerate(raw.get("struct", [])))
    if not structs:
        raise GenError(f"{toml_path}: no [[struct]] entries found")

    known_types = _known_type_names(list(opaque_types), list(structs))

    constant_groups = tuple(
        _parse_constant_group(e, i) for i, e in enumerate(raw.get("constant_group", []))
    )
    if not constant_groups:
        raise GenError(f"{toml_path}: no [[constant_group]] entries found")

    functions_raw = raw.get("function", [])
    if not isinstance(functions_raw, list) or not functions_raw:
        raise GenError(f"{toml_path}: no [[function]] entries found")
    functions = tuple(_parse_function(e, i, known_types) for i, e in enumerate(functions_raw))

    _reject_duplicate_names("opaque_type", [t.name for t in opaque_types])
    _reject_duplicate_names("struct", [s.name for s in structs])
    _reject_duplicate_names("constant_group", [g.name for g in constant_groups])
    _reject_duplicate_names("function", [f.name for f in functions])

    props_defs = gen_props.load_definitions(props_toml_path)

    return Definitions(
        schema_version=schema_version,
        abi_major=abi_major,
        abi_minor=abi_minor,
        opaque_types=opaque_types,
        structs=structs,
        constant_groups=constant_groups,
        functions=functions,
        props=props_defs,
    )


def _reject_duplicate_names(kind: str, names: list[str]) -> None:
    seen: set[str] = set()
    for name in names:
        if name in seen:
            raise GenError(f"duplicate {kind} name '{name}'")
        seen.add(name)


# --------------------------------------------------------------------------
# Return-type classification - see abi/drawgui.def.toml's own header comment
# for why this is a classification over `ret` rather than a per-function
# field: there is no field to leave blank, so every function's wrapping is
# provably uniform by construction rather than by review.
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class ReturnClass:
    cpp_prefix: str  # "return " or "" (void)
    on_oom: str | None  # None means void: no return statement at all
    on_error: str | None


def _classify_return(ret: str) -> ReturnClass:
    if ret == "void":
        return ReturnClass(cpp_prefix="", on_oom=None, on_error=None)
    if ret.endswith("*"):
        return ReturnClass(cpp_prefix="return ", on_oom="nullptr", on_error="nullptr")
    if ret == "int32_t":
        return ReturnClass(cpp_prefix="return ", on_oom="DG_ERR_OOM", on_error="DG_ERR_INTERNAL")
    if ret == "uint32_t":
        return ReturnClass(cpp_prefix="return ", on_oom="0", on_error="0")
    raise GenError(f"no return classification for C type '{ret}' - teach _classify_return")


# --------------------------------------------------------------------------
# Rendering: the C header
# --------------------------------------------------------------------------


def _banner(comment_open: str, comment_close: str = "") -> list[str]:
    rule = "=" * 74
    body = [
        rule,
        f"DO NOT EDIT - generated by {TOOL_NAME} from {SOURCE_NAME}.",
        "",
        f"Edit {SOURCE_NAME} and re-run:",
        f"    python3 {TOOL_NAME}",
        f"`python3 {TOOL_NAME} --check` fails when this file has drifted.",
        rule,
    ]
    if comment_close:
        # Block-comment style ("/*" ... "*/"): the whole banner is one
        # comment, so only the delimiters need to appear on their own lines.
        return [comment_open] + [f" {line}".rstrip() for line in body] + [comment_close]
    # Line-comment style ("//"): every line needs its own marker, or the
    # lines after the first are plain, uncommented C++ text.
    return [f"{comment_open} {line}".rstrip() for line in body]


def _wrap(text: str, width: int = 77) -> list[str]:
    words = text.split()
    lines: list[str] = []
    current = ""
    for word in words:
        candidate = (current + " " + word).strip()
        if len(candidate) > width and current:
            lines.append(current)
            current = word
        else:
            current = candidate
    if current:
        lines.append(current)
    return lines


CLANG_FORMAT_COLUMN_LIMIT = 96


def _wrap_signature(prefix: str, params: tuple[Param, ...], suffix: str) -> list[str]:
    """`prefix` + params + `suffix`, pre-wrapped to match .clang-format's own
    greedy-fill continuation rule for an over-long line (ColumnLimit 96,
    continuation indented to just past the opening paren) - verified against
    a real `clang-format --dry-run` run rather than assumed, because the
    alternative (emitting one line and letting CI's separate clang-format
    job reformat a FILE THIS PROJECT CALLS GENERATED) would silently violate
    "手写任何一份都会导致漂移": a hand-reformatted generated file is still a
    hand edit. Shared by the header's declarations (suffix ");") and the
    trampoline's definitions (suffix ") {")."""
    if not params:
        return [f"{prefix}void){suffix[1:]}"]
    parts = [f"{p.type} {p.name}" for p in params]
    single = prefix + ", ".join(parts) + suffix
    if len(single) <= CLANG_FORMAT_COLUMN_LIMIT:
        return [single]

    indent = " " * len(prefix)
    lines: list[str] = []
    current = prefix
    for i, part in enumerate(parts):
        piece = part + (suffix if i == len(parts) - 1 else ",")
        candidate = current + piece if current in (prefix, indent) else f"{current} {piece}"
        if len(candidate) <= CLANG_FORMAT_COLUMN_LIMIT or current in (prefix, indent):
            current = candidate
        else:
            lines.append(current)
            current = indent + piece
    lines.append(current)
    return lines


def _render_declaration(fn: Function) -> list[str]:
    """A DG_EXPORT declaration in drawgui.h - see _wrap_signature()."""
    return _wrap_signature(f"DG_EXPORT {fn.ret} {fn.name}(", fn.params, ");")
    return lines


def render_header(defs: Definitions) -> str:
    lines = _banner("/*", "*/")
    lines += [
        "",
        "#ifndef DRAWGUI_ABI_DRAWGUI_H",
        "#define DRAWGUI_ABI_DRAWGUI_H",
        "",
        "/* A pure C89-compatible header. No C++ type, no Skia type, and no",
        " * RenderObject/Layer-tree type appears anywhere below - design.md",
        " * section 5.8's own design principle (\"only export the necessary",
        " * surface\"). examples/19_c_client is compiled as C specifically to",
        " * prove this file never quietly grows a C++ leak. */",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "#if defined(_WIN32) && defined(DG_ABI_BUILD_SHARED)",
        "#define DG_EXPORT __declspec(dllexport)",
        "#elif defined(__GNUC__) || defined(__clang__)",
        "#define DG_EXPORT __attribute__((visibility(\"default\")))",
        "#else",
        "#define DG_EXPORT",
        "#endif",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        f"/* MAJOR<<16 | MINOR. Current build: {defs.abi_major}.{defs.abi_minor}",
        " * (design.md section 5.8 decision 4). Compare dg_abi_version() against",
        " * this at link time if the two might disagree (a plugin scenario);",
        " * this project's own examples do not, because they always link the",
        " * library they were built against. */",
        f"#define DG_ABI_VERSION_MAJOR {defs.abi_major}u",
        f"#define DG_ABI_VERSION_MINOR {defs.abi_minor}u",
        "",
    ]

    lines.append("/* -- Opaque handle types " + "-" * 51 + " */")
    lines.append("")
    for opaque in defs.opaque_types:
        for wrapped in _wrap(opaque.summary):
            lines.append(f"/* {wrapped} */")
        lines.append(f"typedef struct {opaque.name[:-2]}_s {opaque.name};")
        lines.append("")

    lines.append("/* -- Structs " + "-" * 63 + " */")
    lines.append("")
    for struct in defs.structs:
        for wrapped in _wrap(struct.summary):
            lines.append(f"/* {wrapped} */")
        lines.append(f"typedef struct {struct.name} {{")
        for f in struct.fields:
            # A comment ABOVE each field, not a same-line trailing one: a
            # trailing comment is only clang-format-stable when every field
            # in the struct is reformatted together to align at one shared
            # column, which this generator would have to replicate exactly
            # (Google style's AlignTrailingComments) or fight on every future
            # field addition. One line above sidesteps the whole alignment
            # problem, matching how every other kind below (opaque types,
            # functions) already documents itself.
            if f.summary:
                lines.append(f"  /* {f.summary} */")
            lines.append(f"  {f.type} {f.name};")
        lines.append(f"}} {struct.name};")
        lines.append("")

    lines.append("/* -- Constants " + "-" * 61 + " */")
    lines.append("")
    for group in defs.constant_groups:
        for wrapped in _wrap(group.summary):
            lines.append(f"/* {wrapped} */")
        for value in group.values:
            if value.summary:
                for wrapped in _wrap(value.summary):
                    lines.append(f"/* {wrapped} */")
            suffix = "u" if group.c_type.startswith("uint") else ""
            literal = f"{value.id}{suffix}"
            # Parenthesized whenever negative: bugprone-macro-parentheses
            # objects to a replacement list that is more than one token
            # (unary minus + digits) and could bind unexpectedly wherever
            # it is pasted - `(-1)` is a single, safe token sequence.
            if value.id < 0:
                literal = f"({literal})"
            lines.append(f"#define {group.prefix}{value.name} {literal}")
        lines.append("")

    lines.append("/* -- Property ids (props/drawgui.props.toml) " + "-" * 30 + " */")
    lines.append("")
    lines.append("/* Re-emitted here in C-compatible #define form from the SAME validated")
    lines.append(" * props/drawgui.props.toml tools/gen_props.py already generates")
    lines.append(" * include/drawgui/render/prop_ids.generated.h from (an `inline constexpr`")
    lines.append(" * C++ form this header cannot use). One source of truth, two renderings -")
    lines.append(" * design.md section 5.8 decision 5.")
    lines.append(" *")
    lines.append(" * #ifndef __cplusplus GUARDS THE WHOLE BLOCK, and that is load-bearing,")
    lines.append(" * not decorative: prop_ids.generated.h ALSO declares `DG_PROP_WIDTH` and")
    lines.append(" * every other name below, as an `inline constexpr dg_prop_id`, not a")
    lines.append(" * macro. A #define of the identical name would silently rewrite THAT")
    lines.append(" * declaration wherever both headers reach the same C++ translation unit")
    lines.append(" * (src/abi/abi_impl.cpp does, needing node_props.h for dg::set_prop()) -")
    lines.append(" * measured, not guessed: an earlier version of this generator produced")
    lines.append(" * exactly `inline constexpr dg_prop_id 1 = 1;` there. A pure C consumer")
    lines.append(" * never includes prop_ids.generated.h (it is not a C header at all), so")
    lines.append(" * excluding these constants from C++ costs a C++ caller nothing it had")
    lines.append(" * another way to get, and the guard is checkable by the one thing that")
    lines.append(" * actually exercises it: examples/19_c_client compiles as C. */")
    lines.append("")
    lines.append("#ifndef __cplusplus")
    lines.append("")
    lines.append("typedef uint16_t dg_prop_id;")
    lines.append("#define DG_PROP_INVALID 0")
    lines.append("")
    for prop in defs.props.properties:
        lines.append(f"/* {prop.summary} */")
        lines.append(f"#define DG_PROP_{prop.name.upper()} {prop.id}")
    lines.append("")
    lines.append("#endif /* !__cplusplus */")
    lines.append("")

    lines.append("/* -- Functions " + "-" * 61 + " */")
    lines.append("")
    for fn in defs.functions:
        for wrapped in _wrap(fn.summary):
            lines.append(f"/* {wrapped} */")
        lines.extend(_render_declaration(fn))
        lines.append("")

    lines += [
        "#ifdef __cplusplus",
        "} /* extern \"C\" */",
        "#endif",
        "",
        "#endif /* DRAWGUI_ABI_DRAWGUI_H */",
        "",
    ]
    return "\n".join(lines)


# --------------------------------------------------------------------------
# Rendering: the trampoline implementation
# --------------------------------------------------------------------------


def render_impl(defs: Definitions) -> str:
    lines = _banner("//")
    lines += [
        "",
        "// Every exported symbol below is generated: design.md section 5.17.1",
        "// forbids a hand-written export function, because a hand-written one",
        "// is the one a person forgets to wrap. `dg::abi::<impl>` (src/abi/",
        "// abi_impl.h/.cpp) is hand-written and free to throw; everything in",
        "// THIS file exists only to make that safe to cross into C from.",
        "",
        '#include "drawgui/abi/drawgui.h"',
        "",
        "#include <new>",
        "#include <stdexcept>",
        "#include <string>",
        "",
        '#include "abi_impl.h"',
        "",
    ]

    for fn in defs.functions:
        cls = _classify_return(fn.ret)
        arg_names = ", ".join(p.name for p in fn.params)
        lines.extend(_wrap_signature(f'extern "C" DG_EXPORT {fn.ret} {fn.name}(', fn.params,
                                     ") {"))
        lines.append("  try {")
        lines.append(f"    {cls.cpp_prefix}dg::abi::{fn.impl}({arg_names});")
        lines.append("  } catch (const std::bad_alloc&) {")
        lines.append(f'    dg::abi::set_last_error("{fn.name}: out of memory");')
        if cls.on_oom is not None:
            lines.append(f"    return {cls.on_oom};")
        lines.append("  } catch (const std::exception& e) {")
        lines.append(f'    dg::abi::set_last_error(std::string("{fn.name}: ") + e.what());')
        if cls.on_error is not None:
            lines.append(f"    return {cls.on_error};")
        lines.append("  } catch (...) {")
        lines.append(f'    dg::abi::set_last_error("{fn.name}: unknown exception");')
        if cls.on_error is not None:
            lines.append(f"    return {cls.on_error};")
        lines.append("  }")
        lines.append("}")
        lines.append("")

    return "\n".join(lines)


# --------------------------------------------------------------------------
# Rendering: TypeScript ambient declarations (P6's future FFI binding reads
# this; nothing in this project consumes it yet - abi/drawgui.def.toml's own
# header comment names why this slice generates it anyway rather than
# deferring a third time).
# --------------------------------------------------------------------------

_C_TO_TS = {
    "void": "void",
    "int32_t": "number",
    "uint32_t": "number",
    "uint16_t": "number",
    "float": "number",
    "const char*": "string | null",
}


def _ts_type(c_type: str, opaque_names: set[str], struct_names: set[str]) -> str:
    if c_type in _C_TO_TS:
        return _C_TO_TS[c_type]
    stripped = c_type.replace("const ", "").rstrip("*").strip()
    if stripped in opaque_names:
        return f"{stripped} | null"
    if stripped in struct_names:
        return stripped
    raise GenError(f"no TypeScript mapping for C type '{c_type}' - teach _ts_type")


def render_dts(defs: Definitions) -> str:
    opaque_names = {t.name for t in defs.opaque_types}
    struct_names = {s.name for s in defs.structs}

    lines = [
        "// " + "=" * 74,
        f"// DO NOT EDIT - generated by {TOOL_NAME} from {SOURCE_NAME}.",
        "//",
        "// Ambient ABI declarations for a future Bun/Node FFI binding (design.md",
        "// section 6, phase P6). NOTHING in this project's own build reads this",
        "// file yet - there is no JS framework layer to bind through - so treat",
        "// it as a settled TARGET shape rather than a proven one; doc/abi.md",
        "// section 6 records that scoping decision explicitly.",
        "// " + "=" * 74,
        "",
    ]

    for opaque in defs.opaque_types:
        for wrapped in _wrap(opaque.summary):
            lines.append(f"// {wrapped}")
        lines.append(f"export type {opaque.name} = unknown;")
        lines.append("")

    for struct in defs.structs:
        for wrapped in _wrap(struct.summary):
            lines.append(f"// {wrapped}")
        lines.append(f"export interface {struct.name} {{")
        for f in struct.fields:
            lines.append(f"  {f.name}: {_ts_type(f.type, opaque_names, struct_names)};")
        lines.append("}")
        lines.append("")

    for group in defs.constant_groups:
        for wrapped in _wrap(group.summary):
            lines.append(f"// {wrapped}")
        lines.append(f"export const {group.name} = {{")
        for value in group.values:
            lines.append(f"  {value.name}: {value.id},")
        lines.append("} as const;")
        lines.append("")

    lines.append("// Property ids - props/drawgui.props.toml, re-emitted (see gen_abi.py).")
    lines.append("export const prop = {")
    for prop in defs.props.properties:
        lines.append(f"  {prop.name}: {prop.id},")
    lines.append("} as const;")
    lines.append("")

    lines.append("export declare namespace DrawguiAbi {")
    for fn in defs.functions:
        for wrapped in _wrap(fn.summary):
            lines.append(f"  // {wrapped}")
        params = ", ".join(
            f"{p.name}: {_ts_type(p.type, opaque_names, struct_names)}" for p in fn.params
        )
        ret = _ts_type(fn.ret, opaque_names, struct_names)
        lines.append(f"  function {fn.name}({params}): {ret};")
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


# --------------------------------------------------------------------------
# Driver
# --------------------------------------------------------------------------


def build_outputs(defs: Definitions, root: Path) -> list[tuple[Path, str]]:
    return [
        (root / HEADER_RELPATH, render_header(defs)),
        (root / IMPL_RELPATH, render_impl(defs)),
        (root / DTS_RELPATH, render_dts(defs)),
    ]


def write_outputs(outputs: list[tuple[Path, str]], root: Path) -> int:
    written = 0
    for path, content in outputs:
        path.parent.mkdir(parents=True, exist_ok=True)
        existing = path.read_text(encoding="utf-8") if path.exists() else None
        if existing == content:
            print(f"unchanged: {path.relative_to(root)}")
            continue
        path.write_text(content, encoding="utf-8")
        print(f"wrote:     {path.relative_to(root)}")
        written += 1
    return written


def check_outputs(outputs: list[tuple[Path, str]], root: Path) -> list[str]:
    drifted: list[str] = []
    for path, content in outputs:
        relative = path.relative_to(root).as_posix()
        if not path.exists():
            print(f"MISSING:   {relative}")
            drifted.append(relative)
            continue
        if path.read_text(encoding="utf-8") != content:
            print(f"STALE:     {relative}")
            drifted.append(relative)
            continue
        print(f"current:   {relative}")
    return drifted


def main(argv: list[str]) -> int:
    default_root = Path(__file__).resolve().parent.parent

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--root", type=Path, default=default_root)
    parser.add_argument("--toml", type=Path, default=None)
    args = parser.parse_args(argv)

    root = args.root.resolve()
    toml_path = (args.toml if args.toml is not None else root / TOML_RELPATH).resolve()
    props_toml_path = root / gen_props.TOML_RELPATH

    try:
        defs = load_definitions(toml_path, props_toml_path)
        outputs = build_outputs(defs, root)
    except GenError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if args.check:
        drifted = check_outputs(outputs, root)
        if drifted:
            print(
                "error: generated file(s) out of date with "
                f"{toml_path.relative_to(root).as_posix()}: {', '.join(drifted)}",
                file=sys.stderr,
            )
            print(f"error: run `python3 {TOOL_NAME}` and commit the result", file=sys.stderr)
            return 1
        print(
            f"{len(defs.functions)} functions, {len(defs.structs)} structs, "
            f"{len(defs.opaque_types)} opaque types; generated files are current"
        )
        return 0

    written = write_outputs(outputs, root)
    print(f"{len(defs.functions)} functions; {written} file(s) updated")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
