#!/usr/bin/env python3
"""Generate drawgui prop_id constants and the property dispatch skeleton.

props/drawgui.props.toml is the single source of truth for every layout and
visual property (design.md section 5.8 decision 5). This script turns it into:

  include/drawgui/render/prop_ids.generated.h   uint16_t prop_id constants
  src/render/prop_dispatch.generated.inc        switch (prop_id) skeleton

Usage:
  python3 tools/gen_props.py            regenerate both outputs
  python3 tools/gen_props.py --check    exit non-zero if either output is
                                        stale, naming the drifted file

The generated C++ contains no hash map and no string-keyed lookup: a property
write is a switch straight into a concrete RenderObject field (section
5.15.3). A name-to-id mapping is a Python-side concern only.
"""

from __future__ import annotations

import argparse
import re
import sys
import tomllib
from dataclasses import dataclass, field as dataclass_field
from pathlib import Path

TOOL_NAME = "tools/gen_props.py"
SOURCE_NAME = "props/drawgui.props.toml"

TOML_RELPATH = Path("props") / "drawgui.props.toml"
HEADER_RELPATH = Path("include") / "drawgui" / "render" / "prop_ids.generated.h"
DISPATCH_RELPATH = Path("src") / "render" / "prop_dispatch.generated.inc"

SUPPORTED_SCHEMA_VERSION = 1

NAME_PATTERN = re.compile(r"^[a-z][a-z0-9_]*$")
MAX_PROP_ID = 0xFFFF

SCALAR_TYPES = ("float", "length", "color", "enum")
COMPLEX_TYPES = ("gradient", "shadow", "transform")
ALL_TYPES = SCALAR_TYPES + COMPLEX_TYPES

PARENT_DATA_SCOPES = ("base", "flex", "stack")

REQUIRED_KEYS = ("name", "id", "type", "group", "field", "summary")
OPTIONAL_KEYS = (
    "values",
    "applies_to",
    "parent_data",
    "parent_data_scope",
    "consumed_by",
    "optional",
)
KNOWN_KEYS = frozenset(REQUIRED_KEYS + OPTIONAL_KEYS)

# The macros the dispatch skeleton emits. Todo 19 defines exactly these; the
# .inc refuses to compile until it does, so a missing definition is loud.
ASSIGN_MACRO = "DG_PROP_ASSIGN"
ASSIGN_COMPLEX_MACRO = "DG_PROP_ASSIGN_COMPLEX"
UNKNOWN_MACRO = "DG_PROP_UNKNOWN"


class GenError(Exception):
    """A fatal problem with the property definitions or the generated files."""


@dataclass(frozen=True)
class Property:
    """One property definition, already validated."""

    name: str
    id: int
    type: str
    group: str
    field: str
    summary: str
    values: tuple[str, ...] = ()
    applies_to: tuple[str, ...] = ()
    parent_data: bool = False
    parent_data_scope: str = ""
    consumed_by: tuple[str, ...] = ()
    optional: bool = False

    @property
    def constant(self) -> str:
        return "DG_PROP_" + self.name.upper()

    @property
    def value_constants(self) -> tuple[tuple[str, int], ...]:
        """The (constant, ordinal) pairs an enum property's `values` become.

        The ordinal is the index in `values`, so the ORDER of that list is as
        much an ABI contract as the id is - a consumer compiles the number,
        not the word. Emitting these is what stops a hand-written copy of the
        list drifting from the TOML; props/prop_ids.lock does not yet cover
        them, which doc/properties.md records as a known gap.
        """
        prefix = "DG_" + self.name.upper() + "_"
        return tuple((prefix + value.upper(), index) for index, value in enumerate(self.values))

    @property
    def is_complex(self) -> bool:
        return self.type in COMPLEX_TYPES

    @property
    def assign_macro(self) -> str:
        return ASSIGN_COMPLEX_MACRO if self.is_complex else ASSIGN_MACRO


@dataclass(frozen=True)
class Definitions:
    """The whole parsed source of truth."""

    schema_version: int
    next_id: int
    node_kinds: tuple[str, ...]
    properties: tuple[Property, ...] = dataclass_field(default=())


# --------------------------------------------------------------------------
# Loading and validation. Every failure raises GenError - nothing is skipped
# quietly, because a silently dropped property would produce a switch that
# silently ignores a write.
# --------------------------------------------------------------------------


def _require_str_list(entry: dict, key: str, where: str) -> tuple[str, ...]:
    raw = entry[key]
    if not isinstance(raw, list) or not raw:
        raise GenError(f"{where}: '{key}' must be a non-empty array of strings")
    for item in raw:
        if not isinstance(item, str) or not item:
            raise GenError(f"{where}: '{key}' must contain only non-empty strings")
    return tuple(raw)


def _parse_property(entry: dict, index: int, node_kinds: tuple[str, ...]) -> Property:
    where = f"[[property]] #{index + 1}"
    if not isinstance(entry, dict):
        raise GenError(f"{where}: expected a table")

    name = entry.get("name")
    if isinstance(name, str) and name:
        where = f"property '{name}'"

    unknown = sorted(set(entry) - KNOWN_KEYS)
    if unknown:
        raise GenError(f"{where}: unknown key(s) {unknown}; known keys are {sorted(KNOWN_KEYS)}")

    missing = [key for key in REQUIRED_KEYS if key not in entry]
    if missing:
        raise GenError(f"{where}: missing required key(s) {missing}")

    if not isinstance(name, str) or not NAME_PATTERN.match(name):
        raise GenError(f"{where}: 'name' must be snake_case matching {NAME_PATTERN.pattern}")

    prop_id = entry["id"]
    if not isinstance(prop_id, int) or isinstance(prop_id, bool):
        raise GenError(f"{where}: 'id' must be an integer")
    if prop_id == 0:
        raise GenError(f"{where}: 'id' 0 is reserved for DG_PROP_INVALID")
    if prop_id < 0 or prop_id > MAX_PROP_ID:
        raise GenError(f"{where}: 'id' {prop_id} is outside the uint16_t range 1..{MAX_PROP_ID}")

    prop_type = entry["type"]
    if prop_type not in ALL_TYPES:
        raise GenError(f"{where}: unknown type '{prop_type}'; expected one of {list(ALL_TYPES)}")

    if prop_type == "enum":
        if "values" not in entry:
            raise GenError(f"{where}: type 'enum' requires 'values'")
        values = _require_str_list(entry, "values", where)
        duplicate_values = sorted({v for v in values if values.count(v) > 1})
        if duplicate_values:
            raise GenError(f"{where}: duplicate enum value(s) {duplicate_values}")
    else:
        if "values" in entry:
            raise GenError(f"{where}: 'values' is only valid for type 'enum'")
        values = ()

    parent_data = entry.get("parent_data", False)
    if not isinstance(parent_data, bool):
        raise GenError(f"{where}: 'parent_data' must be a boolean")

    optional = entry.get("optional", False)
    if not isinstance(optional, bool):
        raise GenError(f"{where}: 'optional' must be a boolean")

    applies_to: tuple[str, ...] = ()
    consumed_by: tuple[str, ...] = ()
    scope = ""

    if parent_data:
        for forbidden in ("applies_to",):
            if forbidden in entry:
                raise GenError(
                    f"{where}: parentData properties use 'consumed_by', not '{forbidden}'"
                )
        if "parent_data_scope" not in entry:
            raise GenError(
                f"{where}: parentData properties require 'parent_data_scope' "
                f"(one of {list(PARENT_DATA_SCOPES)})"
            )
        scope = entry["parent_data_scope"]
        if scope not in PARENT_DATA_SCOPES:
            raise GenError(
                f"{where}: unknown parent_data_scope '{scope}'; "
                f"expected one of {list(PARENT_DATA_SCOPES)}"
            )
        if "consumed_by" not in entry:
            raise GenError(f"{where}: parentData properties require 'consumed_by'")
        consumed_by = _require_str_list(entry, "consumed_by", where)
        unknown_kinds = sorted(set(consumed_by) - set(node_kinds))
        if unknown_kinds:
            raise GenError(f"{where}: 'consumed_by' names unknown node kind(s) {unknown_kinds}")
        if scope == "base" and set(consumed_by) != set(node_kinds) - {"box"}:
            raise GenError(
                f"{where}: parent_data_scope 'base' means every container handles it, "
                f"so 'consumed_by' must list all containers "
                f"{sorted(set(node_kinds) - {'box'})}"
            )
    else:
        for forbidden in ("parent_data_scope", "consumed_by"):
            if forbidden in entry:
                raise GenError(f"{where}: '{forbidden}' requires parent_data = true")
        if "applies_to" not in entry:
            raise GenError(f"{where}: non-parentData properties require 'applies_to'")
        applies_to = _require_str_list(entry, "applies_to", where)
        unknown_kinds = sorted(set(applies_to) - set(node_kinds))
        if unknown_kinds:
            raise GenError(f"{where}: 'applies_to' names unknown node kind(s) {unknown_kinds}")

    field_name = entry["field"]
    if not isinstance(field_name, str) or not NAME_PATTERN.match(field_name):
        raise GenError(f"{where}: 'field' must be snake_case matching {NAME_PATTERN.pattern}")

    group = entry["group"]
    if not isinstance(group, str) or not group:
        raise GenError(f"{where}: 'group' must be a non-empty string")

    summary = entry["summary"]
    if not isinstance(summary, str) or not summary:
        raise GenError(f"{where}: 'summary' must be a non-empty string")
    if "\n" in summary:
        raise GenError(f"{where}: 'summary' must be a single line")

    return Property(
        name=name,
        id=prop_id,
        type=prop_type,
        group=group,
        field=field_name,
        summary=summary,
        values=values,
        applies_to=applies_to,
        parent_data=parent_data,
        parent_data_scope=scope,
        consumed_by=consumed_by,
        optional=optional,
    )


def load_definitions(toml_path: Path) -> Definitions:
    """Parse and fully validate the source of truth."""
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
    node_kinds = _require_str_list(meta, "node_kinds", "[meta]")
    next_id = meta.get("next_id")
    if not isinstance(next_id, int) or isinstance(next_id, bool) or next_id <= 0:
        raise GenError("[meta]: 'next_id' must be a positive integer")

    entries = raw.get("property")
    if not isinstance(entries, list) or not entries:
        raise GenError(f"{toml_path}: no [[property]] entries found")

    properties = [_parse_property(entry, i, node_kinds) for i, entry in enumerate(entries)]

    # Duplicate detection runs before the ordering check so that the reported
    # error names the real problem rather than a symptom of it.
    _reject_duplicates(properties, key=lambda p: p.name, label="name")
    _reject_duplicates(properties, key=lambda p: p.id, label="id")

    for previous, current in zip(properties, properties[1:]):
        if current.id < previous.id:
            raise GenError(
                f"property '{current.name}' has id {current.id} but follows "
                f"'{previous.name}' with id {previous.id}; ids must be listed in "
                f"ascending order so that append-only numbering stays visible"
            )

    highest = max(prop.id for prop in properties)
    if next_id <= highest:
        raise GenError(
            f"[meta]: 'next_id' is {next_id} but id {highest} is already in use; "
            f"next_id must exceed every assigned id"
        )

    _reject_constant_collisions(properties)

    return Definitions(
        schema_version=schema_version,
        next_id=next_id,
        node_kinds=node_kinds,
        properties=tuple(properties),
    )


def _reject_constant_collisions(properties: list[Property]) -> None:
    """No two generated constants may spell the same identifier.

    The header emits both DG_PROP_<NAME> and, for enum properties,
    DG_<NAME>_<VALUE>. Those live in one namespace, and two properties whose
    names and values happen to collide would produce a C++ file that either
    fails to compile or - worse, if the values agreed - silently bound two
    meanings to one spelling. Checked here rather than left to the compiler so
    the error names the two TOML entries instead of a generated line number.
    """
    owners: dict[str, str] = {}
    for prop in properties:
        for constant, owner in [(prop.constant, f"property '{prop.name}'")] + [
            (name, f"property '{prop.name}' value #{ordinal}")
            for name, ordinal in prop.value_constants
        ]:
            if constant in owners:
                raise GenError(
                    f"generated constant '{constant}' would be emitted twice: "
                    f"once for {owners[constant]} and once for {owner}. "
                    f"Rename one of them; a generated constant is an ABI spelling."
                )
            owners[constant] = owner


def _reject_duplicates(properties: list[Property], key, label: str) -> None:
    seen: dict = {}
    for prop in properties:
        value = key(prop)
        if value in seen:
            raise GenError(
                f"duplicate property {label} {value!r}: used by both "
                f"'{seen[value].name}' (id {seen[value].id}) and "
                f"'{prop.name}' (id {prop.id}). "
                f"A property {label} is an ABI contract and is never reused."
            )
        seen[value] = prop


# --------------------------------------------------------------------------
# Rendering.
# --------------------------------------------------------------------------


def _banner(comment: str = "//") -> list[str]:
    rule = f"{comment} " + "=" * 74
    return [
        rule,
        f"{comment} DO NOT EDIT - generated by {TOOL_NAME} from {SOURCE_NAME}.",
        f"{comment}",
        f"{comment} Edit {SOURCE_NAME} and re-run:",
        f"{comment}     python3 {TOOL_NAME}",
        f"{comment} The build regenerates this file, and `python3 {TOOL_NAME} --check`",
        f"{comment} fails when it has drifted, so a stale copy cannot be committed.",
        rule,
    ]


def _scope_note(prop: Property) -> str:
    if not prop.parent_data:
        return "applies_to=" + ",".join(prop.applies_to)
    return (
        f"parentData scope={prop.parent_data_scope} "
        f"consumed_by=" + ",".join(prop.consumed_by)
    )


def render_header(defs: Definitions) -> str:
    """Render the uint16_t prop_id constants."""
    lines = list(_banner())
    lines += [
        "",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "// Property identifiers crossing the C ABI. Numeric, never strings: a frame",
        "// may write thousands of properties and string hashing is pure waste",
        "// (design.md section 5.8 decision 1).",
        "//",
        "// These values are an ABI contract. They are append-only and are never",
        "// reused or renumbered; only a MAJOR version may break them",
        "// (design.md section 5.8 decision 4).",
        "//",
        "// THESE ARE CONSTANTS, NOT AN ENUMERATION, and that is a deliberate",
        "// boundary decision rather than a stylistic one. A prop_id arrives from",
        "// outside the process, so the set of values it may hold is every uint16_t,",
        "// not the set spelled below - and an enumeration type whose variable can",
        "// hold a non-enumerator is exactly the thing C++ says nothing about.",
        "// Measured, on clang-tidy 21 with this project's .clang-tidy, against a",
        "// translation unit that merely INCLUDED the previous `enum dg_prop_id :",
        "// std::uint16_t` form:",
        "//",
        "//   cppcoreguidelines-use-enum-class  enum 'dg_prop_id' is unscoped",
        "//   performance-enum-size             uses a larger base type than",
        "//                                     necessary, consider std::uint8_t",
        "//",
        "// Both are errors under WarningsAsErrors: '*', and obeying either one",
        "// makes the ABI worse. `enum class` makes casting a host-supplied",
        "// uint16_t in the only way to dispatch on it. `std::uint8_t` narrows an",
        "// id the ABI transports as 16 bits, which is the defect already recorded",
        "// against the platform service ids, where 0x0101 folded onto a valid",
        "// enumerator and a query for a service nobody had answered with the",
        "// system tray. Plain constants have neither problem and need no NOLINT:",
        "// the dispatch switches on a uint16_t and its default arm is what decides",
        "// an id is unknown. doc/properties.md records the reasoning in full.",
        "using dg_prop_id = std::uint16_t;",
        "",
        "// Reserved. A write with this id is always an error.",
        "inline constexpr dg_prop_id DG_PROP_INVALID = 0;",
    ]

    current_group = ""
    for prop in defs.properties:
        if prop.group != current_group:
            current_group = prop.group
            lines.append("")
            lines.append(f"// -- {current_group} " + "-" * (60 - len(current_group)))
        lines.append(f"// {prop.summary}")
        detail = f"type={prop.type}"
        if prop.optional:
            detail += " optional"
        lines.append(f"// {detail}; {_scope_note(prop)}")
        if prop.values:
            lines.append("// values: " + " | ".join(prop.values))
        lines.append(f"inline constexpr dg_prop_id {prop.constant} = {prop.id};")

    enum_props = [prop for prop in defs.properties if prop.values]
    if enum_props:
        lines += [
            "",
            "// -- enum property values " + "-" * 46,
            "//",
            "// An enum-typed property travels as an ORDINAL, so the order of each",
            "// `values` list in the TOML is an ABI contract in the same way an id",
            "// is: a consumer compiles the number, not the word. Generating these",
            "// is what stops a hand-written copy of the list drifting from the",
            "// source of truth - the drift class design.md section 5.8 decision 5",
            "// exists to remove.",
            "//",
            "// Plain constants rather than one enum per property, for the reason",
            "// given above: an ordinal also arrives from outside the process.",
            "//",
            "// props/prop_ids.lock does NOT yet cover these orderings.",
            "// doc/properties.md records that as a known gap.",
        ]
        for prop in enum_props:
            lines.append(f"// {prop.name}")
            for constant, ordinal in prop.value_constants:
                lines.append(f"inline constexpr std::uint32_t {constant} = {ordinal};")

    highest = max(prop.id for prop in defs.properties)
    lines += [
        "",
        "// Highest id currently assigned. Boundary code uses it to reject out of",
        "// range ids before dispatching; it grows as properties are appended.",
        f"inline constexpr std::uint16_t kDgPropMaxId = {highest};",
        "",
        "// Number of properties defined. Not an ABI value - retired ids leave gaps,",
        "// so this is not the same as kDgPropMaxId.",
        f"inline constexpr std::uint16_t kDgPropCount = {len(defs.properties)};",
        "",
    ]
    return "\n".join(lines)


def render_dispatch(defs: Definitions) -> str:
    """Render the switch skeleton that writes into concrete struct fields."""
    lines = list(_banner())
    lines += [
        "",
        "// The property dispatch. A prop_id switches straight into a concrete",
        "// RenderObject field: there is deliberately no per-node map<prop_id, value>",
        "// here, because a map costs memory, cache locality and a hash on every",
        "// read, and retrofitting it later is equivalent to rewriting every",
        "// RenderObject (design.md section 5.15.3).",
        "//",
        "// This file is a skeleton. Include it inside a function where `prop_id`",
        "// is in scope, having first defined:",
        "//",
        f"//   {ASSIGN_MACRO}(field, type)",
        "//       Write the incoming scalar value into `field`, interpreting it as",
        f"//       `type` (one of: {', '.join(SCALAR_TYPES)}).",
        f"//   {ASSIGN_COMPLEX_MACRO}(field, type)",
        "//       Write the incoming descriptor into `field`. These types do not fit",
        "//       the scalar tagged union and arrive through a dedicated setter",
        f"//       (design.md section 5.9.5): {', '.join(COMPLEX_TYPES)}.",
        f"//   {UNKNOWN_MACRO}",
        "//       Handle an id this node does not accept. Object-like, and",
        "//       deliberately so: it takes no arguments, and a function-like",
        "//       macro that takes none is one clang-tidy will tell you to",
        "//       write as a function (cppcoreguidelines-macro-usage). The",
        "//       other two paste tokens, which a function cannot do, so they",
        "//       stay function-like.",
        "//       It must report a",
        "//       diagnostic carrying the node path; silently ignoring a write is",
        "//       exactly the failure mode this design forbids (section 5.8",
        "//       decision 7).",
        "",
        f"#if !defined({ASSIGN_MACRO}) || !defined({ASSIGN_COMPLEX_MACRO}) "
        f"|| !defined({UNKNOWN_MACRO})",
        f'#error "prop_dispatch.generated.inc requires {ASSIGN_MACRO}, '
        f'{ASSIGN_COMPLEX_MACRO} and {UNKNOWN_MACRO} to be defined before inclusion"',
        "#endif",
        "",
        "switch (prop_id) {",
    ]

    current_group = ""
    for prop in defs.properties:
        if prop.group != current_group:
            current_group = prop.group
            lines.append(f"  // -- {current_group} " + "-" * (58 - len(current_group)))
        lines.append(f"  case {prop.constant}:")
        lines.append(f"    {prop.assign_macro}({prop.field}, {prop.type});")
        lines.append("    break;")

    lines += [
        "  // No implicit fall-through: an unhandled id is reported, never dropped.",
        "  case DG_PROP_INVALID:",
        "  default:",
        f"    {UNKNOWN_MACRO};",
        "    break;",
        "}",
        "",
    ]
    return "\n".join(lines)


# --------------------------------------------------------------------------
# Driver.
# --------------------------------------------------------------------------


def build_outputs(defs: Definitions, root: Path) -> list[tuple[Path, str]]:
    return [
        (root / HEADER_RELPATH, render_header(defs)),
        (root / DISPATCH_RELPATH, render_dispatch(defs)),
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

    parser = argparse.ArgumentParser(
        description=(
            "Generate drawgui prop_id constants and the property dispatch "
            "skeleton from the single source of truth."
        )
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="do not write; exit non-zero if a generated file has drifted",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=default_root,
        help="repository root (default: the parent of tools/)",
    )
    parser.add_argument(
        "--toml",
        type=Path,
        default=None,
        help=f"path to the property definitions (default: <root>/{TOML_RELPATH.as_posix()})",
    )
    args = parser.parse_args(argv)

    root = args.root.resolve()
    toml_path = (args.toml if args.toml is not None else root / TOML_RELPATH).resolve()

    try:
        defs = load_definitions(toml_path)
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
        print(f"{len(defs.properties)} properties; generated files are current")
        return 0

    written = write_outputs(outputs, root)
    print(f"{len(defs.properties)} properties; {written} file(s) updated")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
