#!/usr/bin/env python3
"""Generate drawgui token_id constants, the loader's name table, and docs.

themes/schema.toml is the single source of truth for which theme tokens
exist (design.md section 5.7.7 - schema is the contract, values are data).
This mirrors tools/gen_props.py exactly in shape: a TOML source of truth, a
generator, and a lock (tools/theme_lock.py) - the same machinery that
already survived a real ABI-drift bug in the property system, reused rather
than reinvented for a second numeric-id family.

Outputs:

  include/drawgui/theme/token_ids.generated.h   uint16_t token_id constants
  src/theme/token_table.generated.inc           the string->id mapping table
                                                 the JSON loader resolves a
                                                 theme.json token NAME through
  doc/theme-tokens.generated.md                 a generated reference table

NOT EMITTED, DEFERRED TO 6-3 (design.md section 5.7.7 names four consumers;
this slice builds two of the four): the ABI numeric-constant table and the
`.d.ts` type hints. Both are meaningless before slice 6-3's C ABI exists at
all - `.d.ts` describes a JS binding that has no host to bind to yet, and an
"ABI constant table" is a table of exported symbols for an ABI this project
has not exported. token_id itself (this file's whole output) is what 6-3
will fold into `prop_id`/`action_id`'s shared generated family, exactly as
doc/animation.md already deferred `curve_id` for the identical reason.

Usage:
  python3 tools/gen_theme.py            regenerate all three outputs
  python3 tools/gen_theme.py --check    exit non-zero if any output is stale
"""

from __future__ import annotations

import argparse
import re
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path

TOOL_NAME = "tools/gen_theme.py"
SOURCE_NAME = "themes/schema.toml"

TOML_RELPATH = Path("themes") / "schema.toml"
HEADER_RELPATH = Path("include") / "drawgui" / "theme" / "token_ids.generated.h"
TABLE_RELPATH = Path("src") / "theme" / "token_table.generated.inc"
DOC_RELPATH = Path("doc") / "theme-tokens.generated.md"
BUILTIN_JSON_RELPATH = Path("themes") / "builtin" / "theme.json"
BUILTIN_HEADER_RELPATH = Path("include") / "drawgui" / "theme" / "builtin_theme.generated.h"

SUPPORTED_SCHEMA_VERSION = 1

# Dotted lower-kebab: "color.primary-hover", "radius.md". The generated
# constant folds '.'/'-' to '_' and uppercases, matching prop_ids' simpler
# ^[a-z][a-z0-9_]*$ rule extended for the dotted token namespace design.md
# section 5.7.2 uses ("color.surface", not "color_surface" - JSON authors
# write the dotted form, and theme.json is meant to be hand-written).
NAME_PATTERN = re.compile(r"^[a-z][a-z0-9]*(\.[a-z][a-z0-9-]*)+$")
MAX_TOKEN_ID = 0xFFFF

TOKEN_TYPES = ("color", "int")

REQUIRED_KEYS = ("name", "id", "type", "group", "summary")
KNOWN_KEYS = frozenset(REQUIRED_KEYS)


class GenError(Exception):
    """A fatal problem with the token definitions or the generated files."""


@dataclass(frozen=True)
class Token:
    name: str
    id: int
    type: str
    group: str
    summary: str

    @property
    def constant(self) -> str:
        mangled = self.name.replace(".", "_").replace("-", "_")
        return "DG_TOKEN_" + mangled.upper()


@dataclass(frozen=True)
class Definitions:
    schema_version: int
    next_id: int
    tokens: tuple[Token, ...]


def _parse_token(entry: dict, index: int) -> Token:
    where = f"[[token]] #{index + 1}"
    if not isinstance(entry, dict):
        raise GenError(f"{where}: expected a table")

    name = entry.get("name")
    if isinstance(name, str) and name:
        where = f"token '{name}'"

    unknown = sorted(set(entry) - KNOWN_KEYS)
    if unknown:
        raise GenError(f"{where}: unknown key(s) {unknown}; known keys are {sorted(KNOWN_KEYS)}")
    missing = [key for key in REQUIRED_KEYS if key not in entry]
    if missing:
        raise GenError(f"{where}: missing required key(s) {missing}")

    if not isinstance(name, str) or not NAME_PATTERN.match(name):
        raise GenError(
            f"{where}: 'name' must be dotted lower-kebab matching {NAME_PATTERN.pattern}"
        )

    token_id = entry["id"]
    if not isinstance(token_id, int) or isinstance(token_id, bool):
        raise GenError(f"{where}: 'id' must be an integer")
    if token_id == 0:
        raise GenError(f"{where}: 'id' 0 is reserved for DG_TOKEN_INVALID")
    if token_id < 0 or token_id > MAX_TOKEN_ID:
        raise GenError(f"{where}: 'id' {token_id} is outside the uint16_t range 1..{MAX_TOKEN_ID}")

    token_type = entry["type"]
    if token_type not in TOKEN_TYPES:
        raise GenError(f"{where}: unknown type '{token_type}'; expected one of {list(TOKEN_TYPES)}")

    group = entry["group"]
    if not isinstance(group, str) or not group:
        raise GenError(f"{where}: 'group' must be a non-empty string")

    summary = entry["summary"]
    if not isinstance(summary, str) or not summary:
        raise GenError(f"{where}: 'summary' must be a non-empty string")
    if "\n" in summary:
        raise GenError(f"{where}: 'summary' must be a single line")

    return Token(name=name, id=token_id, type=token_type, group=group, summary=summary)


def load_definitions(toml_path: Path) -> Definitions:
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
    next_id = meta.get("next_id")
    if not isinstance(next_id, int) or isinstance(next_id, bool) or next_id <= 0:
        raise GenError("[meta]: 'next_id' must be a positive integer")

    entries = raw.get("token")
    if not isinstance(entries, list) or not entries:
        raise GenError(f"{toml_path}: no [[token]] entries found")

    tokens = [_parse_token(entry, i) for i, entry in enumerate(entries)]

    seen_names: dict[str, Token] = {}
    seen_ids: dict[int, Token] = {}
    for tok in tokens:
        if tok.name in seen_names:
            raise GenError(f"duplicate token name {tok.name!r}")
        if tok.id in seen_ids:
            raise GenError(f"duplicate token id {tok.id}")
        seen_names[tok.name] = tok
        seen_ids[tok.id] = tok

    for previous, current in zip(tokens, tokens[1:]):
        if current.id < previous.id:
            raise GenError(
                f"token '{current.name}' has id {current.id} but follows "
                f"'{previous.name}' with id {previous.id}; ids must be listed in "
                f"ascending order so that append-only numbering stays visible"
            )

    highest = max(tok.id for tok in tokens)
    if next_id <= highest:
        raise GenError(
            f"[meta]: 'next_id' is {next_id} but id {highest} is already in use; "
            f"next_id must exceed every assigned id"
        )

    constants: dict[str, Token] = {}
    for tok in tokens:
        if tok.constant in constants:
            raise GenError(
                f"generated constant '{tok.constant}' would be emitted twice: "
                f"once for '{constants[tok.constant].name}' and once for '{tok.name}'"
            )
        constants[tok.constant] = tok

    return Definitions(schema_version=schema_version, next_id=next_id, tokens=tuple(tokens))


def _banner(comment: str = "//") -> list[str]:
    rule = f"{comment} " + "=" * 74
    return [
        rule,
        f"{comment} DO NOT EDIT - generated by {TOOL_NAME} from {SOURCE_NAME}.",
        f"{comment}",
        f"{comment} Edit {SOURCE_NAME} and re-run:",
        f"{comment}     python3 {TOOL_NAME}",
        f"{comment} `python3 {TOOL_NAME} --check` fails when this file has drifted.",
        rule,
    ]


def render_header(defs: Definitions) -> str:
    lines = list(_banner())
    lines += [
        "",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "// Theme token identifiers - the same shape as dg_prop_id",
        "// (include/drawgui/render/prop_ids.generated.h), and for the identical",
        "// reason: PLAIN CONSTANTS, NOT AN ENUMERATION. A token_id arrives from a",
        "// theme.json's string name via a runtime lookup table (token_table.",
        "// generated.inc below), so the set of values a variable of this type may",
        "// hold is every uint16_t, not the set spelled out here - an enum class",
        "// would claim otherwise and clang-tidy's cppcoreguidelines-use-enum-class /",
        "// performance-enum-size would flag the previous enum form the same way",
        "// prop_ids.generated.h already records for prop_id.",
        "//",
        "// APPEND-ONLY, per themes/token_ids.lock (design.md section 5.7.7 /",
        "// section 5.8 decision 4, the same ABI-numbering rule props/prop_ids.lock",
        "// already enforces for prop_id).",
        "using dg_token_id = std::uint16_t;",
        "",
        "// Reserved. A lookup returning this id means \"no such token\".",
        "inline constexpr dg_token_id DG_TOKEN_INVALID = 0;",
    ]

    current_group = ""
    for tok in defs.tokens:
        if tok.group != current_group:
            current_group = tok.group
            lines.append("")
            lines.append(f"// -- {current_group} " + "-" * (60 - len(current_group)))
        lines.append(f"// {tok.summary}")
        lines.append(f"// type={tok.type}")
        lines.append(f'inline constexpr dg_token_id {tok.constant} = {tok.id};  // "{tok.name}"')

    highest = max(tok.id for tok in defs.tokens)
    lines += [
        "",
        "inline constexpr std::uint16_t kDgTokenMaxId = " + str(highest) + ";",
        "inline constexpr std::uint16_t kDgTokenCount = " + str(len(defs.tokens)) + ";",
        "",
    ]
    return "\n".join(lines)


def render_table(defs: Definitions) -> str:
    """The loader's string->id mapping table, as raw TokenEntry initializers.

    Emitted as plain `{"name", DG_TOKEN_X, TokenType::k_type},` lines rather
    than through an X-macro (DG_TOKEN_ENTRY(name, id, type)): a function-like
    macro whose whole body is a single value expression is exactly what
    clang-tidy's cppcoreguidelines-macro-usage flags as replaceable, and here
    it genuinely is replaceable - unlike prop_dispatch.generated.inc's
    DG_PROP_ASSIGN (whose body is a full statement executed inside a
    `switch`, not a value), this table has exactly one reader
    (src/theme/theme.cpp's kTokenTable) and no control-flow shape to paste
    into, so emitting the literal initializer directly is both simpler and
    the form clang-tidy accepts without a suppression. `name` is the DOTTED
    STRING a theme.json author writes ("color.surface"), not the generated
    constant - this is the one place that string appears in generated C++,
    so the loader's name->id resolution has exactly one table to drift from
    the schema, and it cannot: this file is generated FROM it.
    """
    lines = list(_banner())
    lines += [""]
    for tok in defs.tokens:
        lines.append(f'{{"{tok.name}", {tok.constant}, TokenType::k_{tok.type}}},')
    lines.append("")
    return "\n".join(lines)


def render_doc(defs: Definitions) -> str:
    lines = [
        "<!--",
        f"DO NOT EDIT - generated by {TOOL_NAME} from {SOURCE_NAME}.",
        f"Edit {SOURCE_NAME} and re-run `python3 {TOOL_NAME}`.",
        "-->",
        "",
        "# Theme token reference (generated)",
        "",
        "One row per token in `themes/schema.toml`. `doc/theme.md` is the",
        "hand-written narrative this table supports; this file is regenerated",
        "on every change, never edited directly.",
        "",
        "| id | name | type | group | summary |",
        "| -- | ---- | ---- | ----- | ------- |",
    ]
    for tok in defs.tokens:
        lines.append(f"| {tok.id} | `{tok.name}` | {tok.type} | {tok.group} | {tok.summary} |")
    lines.append("")
    return "\n".join(lines)


def build_outputs(defs: Definitions, root: Path) -> list[tuple[Path, str]]:
    return [
        (root / HEADER_RELPATH, render_header(defs)),
        (root / TABLE_RELPATH, render_table(defs)),
        (root / DOC_RELPATH, render_doc(defs)),
        (root / BUILTIN_HEADER_RELPATH, render_builtin_header(root)),
    ]


def render_builtin_header(root: Path) -> str:
    """Embed themes/builtin/theme.json as a string, verbatim.

    design.md section 5.7.4: "内置主题同样是 JSON，以字符串嵌入二进制" - the
    builtin theme ships inside the binary rather than as a file the process
    reads at startup, so there is exactly one loading path (dg::load_theme(),
    the same function a future external theme package would call) and no
    "file not found next to the executable" failure mode for the one theme
    this project ships. tools/check_consistency.py verifies, at CI time, that
    this JSON covers every token themes/schema.toml declares - the two files
    are hand-authored independently and could otherwise drift exactly the way
    stonegui's four token files did.
    """
    json_path = root / BUILTIN_JSON_RELPATH
    if not json_path.exists():
        raise GenError(f"missing builtin theme: {json_path}")
    raw = json_path.read_text(encoding="utf-8")
    if ")DGTHEMEJSON\"" in raw:
        raise GenError(f"{BUILTIN_JSON_RELPATH}: contains the raw-string delimiter; pick another")
    lines = list(_banner())
    lines += [
        "",
        "#pragma once",
        "",
        "namespace dg {",
        "",
        f"// Embedded verbatim from {BUILTIN_JSON_RELPATH.as_posix()}.",
        'inline constexpr char kBuiltinThemeJson[] = R"DGTHEMEJSON(',
        raw.rstrip("\n"),
        ')DGTHEMEJSON";',
        "",
        "}  // namespace dg",
        "",
    ]
    return "\n".join(lines)


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
        print(f"{len(defs.tokens)} tokens; generated files are current")
        return 0

    written = write_outputs(outputs, root)
    print(f"{len(defs.tokens)} tokens; {written} file(s) updated")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
