#!/usr/bin/env python3
"""Generate drawgui action_id constants, LogicalKey, ActionScope and the
per-action binding table.

input/shortcuts.toml is the single source of truth for which shortcut/intent
actions exist (design.md section 5.5.1-5.5.3). This mirrors tools/gen_theme.py
and tools/gen_props.py exactly in shape: a TOML source of truth, a generator,
and a lock (tools/shortcut_lock.py) - the fourth numeric-id family alongside
props/, themes/, abi/.

Outputs:

  include/drawgui/shortcuts/action_ids.generated.h    dg_action_id constants
  include/drawgui/shortcuts/logical_key.generated.h   the LogicalKey enum
  include/drawgui/shortcuts/action_scope.generated.h  the ActionScope enum
  src/shortcuts/logical_key_table.generated.inc       name -> LogicalKey table
  src/shortcuts/binding_table.generated.inc           action -> chord table
  doc/shortcuts.generated.md                          a generated reference

This generator also performs design.md section 7's shortcut CI check, as
part of ordinary validation (a malformed table is a generation failure, the
same way an out-of-order id already is for props/themes):

  1. no two actions bound to the same chord in the same scope
  2. every action resolves to a binding on all three platforms (linux,
     windows, macos) - trivially true while every binding is `Mod`-derived,
     but a REAL check: it fails loudly if the platform-substitution table
     (PLATFORM_MOD below) is ever missing an entry, which is exactly the
     maintenance mistake this check exists to catch.

Usage:
  python3 tools/gen_shortcuts.py            regenerate all outputs
  python3 tools/gen_shortcuts.py --check    exit non-zero if any output is stale
"""

from __future__ import annotations

import argparse
import re
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path

TOOL_NAME = "tools/gen_shortcuts.py"
SOURCE_NAME = "input/shortcuts.toml"

TOML_RELPATH = Path("input") / "shortcuts.toml"
ACTION_HEADER_RELPATH = Path("include") / "drawgui" / "shortcuts" / "action_ids.generated.h"
LOGICAL_KEY_HEADER_RELPATH = Path("include") / "drawgui" / "shortcuts" / "logical_key.generated.h"
ACTION_SCOPE_HEADER_RELPATH = Path("include") / "drawgui" / "shortcuts" / "action_scope.generated.h"
LOGICAL_KEY_TABLE_RELPATH = Path("src") / "shortcuts" / "logical_key_table.generated.inc"
BINDING_TABLE_RELPATH = Path("src") / "shortcuts" / "binding_table.generated.inc"
DOC_RELPATH = Path("doc") / "shortcuts.generated.md"

SUPPORTED_SCHEMA_VERSION = 1

NAME_PATTERN = re.compile(r"^[a-z][a-z0-9_]*$")
KEY_NAME_PATTERN = re.compile(r"^[A-Z][A-Za-z0-9]*$")
MAX_ACTION_ID = 0xFFFF

# Fixed engineering vocabulary, NOT generated from the TOML - a chord's
# grammar (which modifier words exist, and in what order) is part of this
# generator's contract with tools/../include/drawgui/shortcuts/chord.h, not
# data a table author chooses freely. Every action's key IS generated
# (LogicalKey below), because that vocabulary genuinely is "whatever a
# binding referenced"; the three modifier names are not.
MODIFIER_ORDER = ("Mod", "Shift", "Alt")

# design.md section 5.5.1: `Mod` is macOS's Cmd, everyone else's Ctrl. The
# ONLY per-platform data this generator holds - see the module docstring's
# check 2.
PLATFORM_MOD = {
    "linux": "Ctrl",
    "windows": "Ctrl",
    "macos": "Cmd",
}
PLATFORMS = tuple(PLATFORM_MOD)

REQUIRED_KEYS = ("name", "id", "scope", "consumer", "binding", "summary")
KNOWN_KEYS = frozenset(REQUIRED_KEYS)


class GenError(Exception):
    """A fatal problem with the shortcut definitions or the generated files."""


@dataclass(frozen=True)
class Action:
    name: str
    id: int
    scope: str
    consumer: str
    binding: str
    summary: str
    mods: frozenset[str]
    key: str

    @property
    def constant(self) -> str:
        return "DG_ACTION_" + self.name.upper()

    @property
    def scope_constant(self) -> str:
        return "ActionScope::k" + _pascal_case(self.scope)

    @property
    def key_constant(self) -> str:
        return "LogicalKey::k" + self.key

    @property
    def mods_expr(self) -> str:
        if not self.mods:
            return "Modifier::kNone"
        return " | ".join(f"Modifier::k{mod}" for mod in MODIFIER_ORDER if mod in self.mods)


@dataclass(frozen=True)
class Definitions:
    schema_version: int
    next_id: int
    actions: tuple[Action, ...]

    @property
    def logical_keys(self) -> tuple[str, ...]:
        return tuple(sorted({action.key for action in self.actions}))

    @property
    def scopes(self) -> tuple[str, ...]:
        return tuple(sorted({action.scope for action in self.actions}))


def _pascal_case(snake: str) -> str:
    return "".join(part.capitalize() for part in snake.split("_"))


def _parse_binding(binding: str, where: str) -> tuple[frozenset[str], str]:
    if not binding:
        raise GenError(f"{where}: 'binding' must not be empty")
    parts = binding.split("+")
    if any(part == "" for part in parts):
        raise GenError(f"{where}: binding {binding!r} has an empty '+'-separated segment")

    *mod_parts, key = parts
    seen: list[str] = []
    for mod in mod_parts:
        if mod not in MODIFIER_ORDER:
            raise GenError(
                f"{where}: binding {binding!r} has unknown modifier {mod!r}; "
                f"expected one of {list(MODIFIER_ORDER)}"
            )
        if mod in seen:
            raise GenError(f"{where}: binding {binding!r} repeats modifier {mod!r}")
        seen.append(mod)

    canonical = [mod for mod in MODIFIER_ORDER if mod in seen]
    if seen != canonical:
        raise GenError(
            f"{where}: binding {binding!r} modifiers must appear in "
            f"{'+'.join(MODIFIER_ORDER)} order, found {'+'.join(seen)}"
        )

    if not KEY_NAME_PATTERN.match(key):
        raise GenError(
            f"{where}: binding {binding!r} key {key!r} must match {KEY_NAME_PATTERN.pattern}"
        )

    return frozenset(seen), key


def _parse_action(entry: dict, index: int) -> Action:
    where = f"[[action]] #{index + 1}"
    if not isinstance(entry, dict):
        raise GenError(f"{where}: expected a table")

    name = entry.get("name")
    if isinstance(name, str) and name:
        where = f"action '{name}'"

    unknown = sorted(set(entry) - KNOWN_KEYS)
    if unknown:
        raise GenError(f"{where}: unknown key(s) {unknown}; known keys are {sorted(KNOWN_KEYS)}")
    missing = [key for key in REQUIRED_KEYS if key not in entry]
    if missing:
        raise GenError(f"{where}: missing required key(s) {missing}")

    if not isinstance(name, str) or not NAME_PATTERN.match(name):
        raise GenError(f"{where}: 'name' must match {NAME_PATTERN.pattern}")

    action_id = entry["id"]
    if not isinstance(action_id, int) or isinstance(action_id, bool):
        raise GenError(f"{where}: 'id' must be an integer")
    if action_id == 0:
        raise GenError(f"{where}: 'id' 0 is reserved for DG_ACTION_INVALID")
    if action_id < 0 or action_id > MAX_ACTION_ID:
        raise GenError(
            f"{where}: 'id' {action_id} is outside the uint16_t range 1..{MAX_ACTION_ID}"
        )

    scope = entry["scope"]
    if not isinstance(scope, str) or not NAME_PATTERN.match(scope):
        raise GenError(f"{where}: 'scope' must match {NAME_PATTERN.pattern}")

    # THE MANDATORY FIELD (task's own "no speculative vocabulary" rule):
    # an action with no named consumer is exactly the "interface written
    # ahead of an implementation" this project's standing rule forbids, made
    # mechanical rather than left to a reviewer to notice.
    consumer = entry["consumer"]
    if not isinstance(consumer, str) or not consumer.strip():
        raise GenError(f"{where}: 'consumer' must be a non-empty string naming what consumes it")

    binding = entry["binding"]
    if not isinstance(binding, str):
        raise GenError(f"{where}: 'binding' must be a string")
    mods, key = _parse_binding(binding, where)

    summary = entry["summary"]
    if not isinstance(summary, str) or not summary:
        raise GenError(f"{where}: 'summary' must be a non-empty string")
    if "\n" in summary:
        raise GenError(f"{where}: 'summary' must be a single line")

    return Action(
        name=name,
        id=action_id,
        scope=scope,
        consumer=consumer,
        binding=binding,
        summary=summary,
        mods=mods,
        key=key,
    )


def _check_no_duplicate_bindings(actions: tuple[Action, ...]) -> None:
    """design.md section 7, check 1: no two actions share a chord in a scope."""
    seen: dict[tuple[str, frozenset[str], str], Action] = {}
    for action in actions:
        chord_key = (action.scope, action.mods, action.key)
        earlier = seen.get(chord_key)
        if earlier is not None:
            raise GenError(
                f"actions '{earlier.name}' and '{action.name}' are both bound to "
                f"'{action.binding}' in scope '{action.scope}'; design.md section 7 "
                f"forbids two actions sharing a chord within one scope"
            )
        seen[chord_key] = action


def _check_every_platform_bound(actions: tuple[Action, ...]) -> None:
    """design.md section 7, check 2: every action resolves on every platform."""
    for action in actions:
        for platform in PLATFORMS:
            try:
                label = resolve_label(action.mods, action.key, platform)
            except KeyError as exc:
                raise GenError(
                    f"action '{action.name}' has no resolved binding for platform "
                    f"'{platform}': {exc}"
                ) from exc
            if not label:
                raise GenError(
                    f"action '{action.name}' resolved to an empty label on '{platform}'"
                )


MAC_ORDER = ("Shift", "Alt", "Mod")
MAC_SYMBOLS = {"Mod": "\u2318", "Shift": "\u21e7", "Alt": "\u2325"}


def resolve_label(mods: frozenset[str], key: str, platform: str) -> str:
    """The menu-accelerator text design.md section 5.5.1 requires GENERATED,
    never hand-written. Also the mechanism check 2 above proves: this raises
    KeyError the moment PLATFORM_MOD stops covering `platform`.
    """
    mod_word = PLATFORM_MOD[platform]
    if platform == "macos":
        symbols = "".join(MAC_SYMBOLS[mod] for mod in MAC_ORDER if mod in mods)
        return symbols + key
    words = [mod_word if mod == "Mod" else mod for mod in MODIFIER_ORDER if mod in mods]
    return "+".join([*words, key])


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

    entries = raw.get("action")
    if not isinstance(entries, list) or not entries:
        raise GenError(f"{toml_path}: no [[action]] entries found")

    actions = [_parse_action(entry, i) for i, entry in enumerate(entries)]

    seen_names: dict[str, Action] = {}
    seen_ids: dict[int, Action] = {}
    for action in actions:
        if action.name in seen_names:
            raise GenError(f"duplicate action name {action.name!r}")
        if action.id in seen_ids:
            raise GenError(f"duplicate action id {action.id}")
        seen_names[action.name] = action
        seen_ids[action.id] = action

    for previous, current in zip(actions, actions[1:]):
        if current.id < previous.id:
            raise GenError(
                f"action '{current.name}' has id {current.id} but follows "
                f"'{previous.name}' with id {previous.id}; ids must be listed in "
                f"ascending order so that append-only numbering stays visible"
            )

    highest = max(action.id for action in actions)
    if next_id <= highest:
        raise GenError(
            f"[meta]: 'next_id' is {next_id} but id {highest} is already in use; "
            f"next_id must exceed every assigned id"
        )

    actions_tuple = tuple(actions)
    _check_no_duplicate_bindings(actions_tuple)
    _check_every_platform_bound(actions_tuple)

    return Definitions(schema_version=schema_version, next_id=next_id, actions=actions_tuple)


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


def render_action_header(defs: Definitions) -> str:
    lines = list(_banner())
    lines += [
        "",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "// Shortcut/intent action identifiers - the same shape as dg_prop_id and",
        "// dg_token_id (design.md section 5.5.3: action_id is \"同源同构\" with",
        "// prop_id/token_id) and for the identical reason: PLAIN CONSTANTS, NOT AN",
        "// ENUMERATION. An action_id will cross the C ABI once 8-3d wires",
        "// dg_app_bind_shortcut/dg_node_scope_action into abi/drawgui.def.toml, so",
        "// the set of values a variable of this type may hold is every uint16_t a",
        "// host may pass in, not the set spelled out here - an enum class would",
        "// claim otherwise, exactly the reasoning prop_ids.generated.h and",
        "// token_ids.generated.h already record for their own families.",
        "//",
        "// APPEND-ONLY, per input/action_ids.lock (design.md section 5.5.3, the",
        "// same ABI-numbering rule props/prop_ids.lock and themes/token_ids.lock",
        "// already enforce for prop_id/token_id).",
        "using dg_action_id = std::uint16_t;",
        "",
        "// Reserved. Names \"no such action\" the way DG_PROP_INVALID/DG_TOKEN_INVALID do.",
        "inline constexpr dg_action_id DG_ACTION_INVALID = 0;",
        "",
    ]
    for action in defs.actions:
        lines.append(f"// {action.summary}")
        lines.append(f"// scope={action.scope} consumer=\"{action.consumer}\"")
        lines.append(
            f'inline constexpr dg_action_id {action.constant} = {action.id};  // "{action.binding}"'
        )
        lines.append("")

    highest = max(action.id for action in defs.actions)
    lines += [
        f"inline constexpr std::uint16_t kDgActionMaxId = {highest};",
        f"inline constexpr std::uint16_t kDgActionCount = {len(defs.actions)};",
        "",
    ]
    return "\n".join(lines)


def render_logical_key_header(defs: Definitions) -> str:
    lines = list(_banner())
    lines += [
        "",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "// The closed set of physical/logical keys a shortcut binding in",
        "// input/shortcuts.toml actually references - GENERATED, unlike",
        "// dg_action_id above, because this is the opposite ABI shape: a",
        "// LogicalKey is produced internally (the platform layer mapping an SDL",
        "// keysym, once 8-2 exists) and never arrives as an arbitrary",
        "// host-supplied number, so it stays a real `enum class` with an",
        "// EXHAUSTIVE-SWITCH contract - the same shape dg::Key, dg::WidgetKind",
        "// and dg::PointerAction already have, and for the identical reason",
        "// dg::Key's own comment gives: an enumerator naming a key nothing binds",
        "// is a promise this engine does not keep. This is NOT dg::Key",
        "// (include/drawgui/window/window_manager.h) - design.md section 5.5.2 is",
        "// explicit that a text-editing key is not a shortcut; they route at",
        "// different levels and this project does not unify them.",
        "namespace dg {",
        "",
        "enum class LogicalKey : std::uint16_t {",
        "  kInvalid = 0,",
    ]
    for key in defs.logical_keys:
        lines.append(f"  k{key},")
    lines += [
        "};",
        "",
        f"inline constexpr std::uint16_t kLogicalKeyCount = {len(defs.logical_keys)};",
        "",
        "}  // namespace dg",
        "",
    ]
    return "\n".join(lines)


def render_action_scope_header(defs: Definitions) -> str:
    lines = list(_banner())
    lines += [
        "",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "// The routing-level scopes design.md section 5.5.2 names, GENERATED from",
        "// exactly the `scope` values input/shortcuts.toml's actions reference -",
        "// the same \"no speculative vocabulary\" policy LogicalKey follows above.",
        "// A closed, internal set (never crosses the C ABI as a raw number), so",
        "// this is a real `enum class`, not a plain-constant family like",
        "// dg_action_id.",
        "namespace dg {",
        "",
        "enum class ActionScope : std::uint8_t {",
    ]
    for scope in defs.scopes:
        lines.append(f"  k{_pascal_case(scope)},")
    lines += [
        "};",
        "",
        "}  // namespace dg",
        "",
    ]
    return "\n".join(lines)


def render_logical_key_table(defs: Definitions) -> str:
    """name -> LogicalKey, for src/shortcuts/chord.cpp's parse_chord() to
    resolve a chord string's key segment against exactly the keys this table
    (and therefore the LogicalKey enum above) actually declares - the same
    "one table, generated, nothing else may drift from it" shape
    tools/gen_theme.py's render_table() already uses for token_table.
    generated.inc.
    """
    lines = list(_banner())
    lines += [""]
    for key in defs.logical_keys:
        lines.append(f'{{"{key}", LogicalKey::k{key}}},')
    lines.append("")
    return "\n".join(lines)


def render_binding_table(defs: Definitions) -> str:
    """The action -> chord table src/shortcuts/chord.cpp's ShortcutBinding
    array is built from. `Chord` stays symbolic on `Modifier::kMod` for
    every platform - resolving it to a concrete, comparable-against-a-real-
    KeyEvent bit is 8-2/8-3's job once the modifier bitmask exists; this
    table's own per-platform contribution is entirely in the LABEL text
    format_chord()/mod_label() compute from it, not in a second copy of the
    chord itself.
    """
    lines = list(_banner())
    lines += [""]
    for action in defs.actions:
        lines.append(
            "{"
            f"{action.constant}, {action.scope_constant}, "
            f"Chord{{{action.mods_expr}, {action.key_constant}}}, "
            f'"{action.name}", "{action.consumer}"'
            "},"
        )
    lines.append("")
    return "\n".join(lines)


def render_doc(defs: Definitions) -> str:
    lines = [
        "<!--",
        f"DO NOT EDIT - generated by {TOOL_NAME} from {SOURCE_NAME}.",
        f"Edit {SOURCE_NAME} and re-run `python3 {TOOL_NAME}`.",
        "-->",
        "",
        "# Shortcut/intent action reference (generated)",
        "",
        "One row per `[[action]]` in `input/shortcuts.toml`. `doc/shortcuts.md`",
        "(8-6) is the hand-written narrative this table supports; this file is",
        "regenerated on every change, never edited directly.",
        "",
        "| id | name | scope | binding | linux/windows | macos | consumer |",
        "| -- | ---- | ----- | ------- | -------------- | ----- | -------- |",
    ]
    for action in defs.actions:
        linux_label = resolve_label(action.mods, action.key, "linux")
        macos_label = resolve_label(action.mods, action.key, "macos")
        lines.append(
            f"| {action.id} | `{action.name}` | {action.scope} | `{action.binding}` | "
            f"{linux_label} | {macos_label} | {action.consumer} |"
        )
    lines.append("")
    return "\n".join(lines)


def build_outputs(defs: Definitions, root: Path) -> list[tuple[Path, str]]:
    return [
        (root / ACTION_HEADER_RELPATH, render_action_header(defs)),
        (root / LOGICAL_KEY_HEADER_RELPATH, render_logical_key_header(defs)),
        (root / ACTION_SCOPE_HEADER_RELPATH, render_action_scope_header(defs)),
        (root / LOGICAL_KEY_TABLE_RELPATH, render_logical_key_table(defs)),
        (root / BINDING_TABLE_RELPATH, render_binding_table(defs)),
        (root / DOC_RELPATH, render_doc(defs)),
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
        print(f"{len(defs.actions)} actions; generated files are current")
        return 0

    written = write_outputs(outputs, root)
    print(f"{len(defs.actions)} actions; {written} file(s) updated")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
