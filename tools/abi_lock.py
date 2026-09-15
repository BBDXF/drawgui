#!/usr/bin/env python3
"""Guard the C ABI stability rules recorded in abi/drawgui_abi.lock.

tools/gen_abi.py answers "do the generated files match abi/drawgui.def.toml?".
It cannot answer "has the definition broken its own ABI contract?", because
changing dg_node_set_prop's signature, or reordering dg_window_opts's fields,
regenerates perfectly happily and silently breaks every already-compiled
consumer - the identical gap tools/prop_lock.py exists to close for prop_id,
generalised to three more entity kinds a C ABI actually has: functions,
structs and constants (design.md section 5.8 decision 4).

abi/drawgui_abi.lock is the committed snapshot this script compares
abi/drawgui.def.toml against:

  function        renamed, removed, or re-signed (params/return changed)  FAIL
                   added (new name, any signature)                        ALLOWED
  struct           a locked field's name/type/position changed,
                   or the struct now has FEWER fields than locked         FAIL
                   new fields appended at the end                        ALLOWED
  constant_group   a locked value renumbered, renamed, or removed         FAIL
                   a value appended with a fresh id                      ALLOWED
  opaque_type      removed or renamed                                    FAIL
                   added                                                 ALLOWED

Every one of the four is MAJOR except "appended", which is MINOR and always
permitted (design.md section 5.8 decision 4). --write records the current
definitions; like tools/prop_lock.py, it runs the same comparison first, so
it can only ever record an addition, never paper over a break.

Usage:
  python3 tools/abi_lock.py --check    verify abi/drawgui.def.toml against the lock
  python3 tools/abi_lock.py --write    record the current definitions; refuses
                                       when the change is a break
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gen_abi import (  # noqa: E402  (needs the sys.path line above)
    TOML_RELPATH,
    Definitions,
    GenError,
    load_definitions,
)
import gen_props  # noqa: E402

TOOL_NAME = "tools/abi_lock.py"
LOCK_NAME = "abi/drawgui_abi.lock"
LOCK_RELPATH = Path("abi") / "drawgui_abi.lock"
SOURCE_NAME = "abi/drawgui.def.toml"

LOCK_HEADER = f"""\
# ============================================================================
# drawgui C ABI stability lock - an ABI contract, not a cache.
#
# Written by `python3 {TOOL_NAME} --write` and verified by
# `python3 {TOOL_NAME} --check`. See this tool's own module docstring for
# exactly what each of the four sections below guards.
#
# NEVER hand-edit this file to make a failing check pass. The check is
# reporting that {SOURCE_NAME} broke its own ABI contract; editing the
# ledger does not repair the break, it only hides it from the next reviewer.
# A genuinely intended break is a reviewed MAJOR version change that says so
# out loud (design.md section 5.8 decision 4), never a quiet edit here.
# ============================================================================
"""


def _fn_signature(fn) -> str:
    params = ",".join(p.type for p in fn.params)
    return f"{fn.ret}({params})"


def _struct_fields(struct) -> list[str]:
    return [f"{f.name}:{f.type}" for f in struct.fields]


def render_lock(defs: Definitions) -> str:
    lines = [LOCK_HEADER.rstrip("\n"), ""]

    lines.append("[opaque_type]")
    for t in defs.opaque_types:
        lines.append(t.name)
    lines.append("")

    lines.append("[struct]")
    for s in defs.structs:
        lines.append(f"{s.name}: " + ",".join(_struct_fields(s)))
    lines.append("")

    lines.append("[constant_group]")
    for g in defs.constant_groups:
        for v in g.values:
            lines.append(f"{g.name}.{v.name}={v.id}")
    lines.append("")

    lines.append("[function]")
    for fn in defs.functions:
        lines.append(f"{fn.name}: {_fn_signature(fn)}")
    lines.append("")

    return "\n".join(lines) + "\n"


class LockedState:
    def __init__(self):
        self.opaque_types: set[str] = set()
        self.struct_fields: dict[str, list[str]] = {}
        self.constants: dict[str, int] = {}  # "group.name" -> id
        self.functions: dict[str, str] = {}  # name -> signature


def parse_lock(text: str, where: str) -> LockedState:
    state = LockedState()
    section = None
    for lineno, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        at = f"{where}:{lineno}"
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        if section == "opaque_type":
            state.opaque_types.add(line)
        elif section == "struct":
            name, _, fields = line.partition(":")
            name = name.strip()
            if not name or not fields:
                raise GenError(f"{at}: malformed struct entry {raw!r}")
            state.struct_fields[name] = [f.strip() for f in fields.split(",") if f.strip()]
        elif section == "constant_group":
            key, _, value = line.partition("=")
            if not key or not value:
                raise GenError(f"{at}: malformed constant entry {raw!r}")
            state.constants[key.strip()] = int(value.strip())
        elif section == "function":
            name, _, sig = line.partition(":")
            name = name.strip()
            sig = sig.strip()
            if not name or not sig:
                raise GenError(f"{at}: malformed function entry {raw!r}")
            state.functions[name] = sig
        else:
            raise GenError(f"{at}: entry outside any [section]: {raw!r}")
    return state


def load_lock(lock_path: Path, root: Path) -> LockedState | None:
    if not lock_path.exists():
        return None
    return parse_lock(lock_path.read_text(encoding="utf-8"), _relative(lock_path, root))


def _relative(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def compare(defs: Definitions, locked: LockedState) -> tuple[list[str], bool]:
    """Return (violations, appended_anything)."""
    violations: list[str] = []
    appended = False

    current_opaque = {t.name for t in defs.opaque_types}
    for name in sorted(locked.opaque_types):
        if name not in current_opaque:
            violations.append(
                f"opaque_type '{name}' is locked but is gone from {SOURCE_NAME}. "
                f"An opaque type is never removed or renamed; restore it."
            )
    if current_opaque - locked.opaque_types:
        appended = True

    current_structs = {s.name: _struct_fields(s) for s in defs.structs}
    for name, locked_fields in locked.struct_fields.items():
        current_fields = current_structs.get(name)
        if current_fields is None:
            violations.append(
                f"struct '{name}' is locked but is gone from {SOURCE_NAME}. "
                f"A struct is never removed; restore it."
            )
            continue
        if len(current_fields) < len(locked_fields) or current_fields[: len(locked_fields)] != locked_fields:
            violations.append(
                f"struct '{name}': locked fields {locked_fields} are no longer an "
                f"unchanged prefix of the current fields {current_fields}. A struct's "
                f"existing fields may only be APPENDED to, never reordered, retyped, "
                f"renamed or removed (design.md section 5.8 decision 4) - this is what "
                f"lets an old, shorter `size` still describe a valid prefix."
            )
    for name, current_fields in current_structs.items():
        locked_fields = locked.struct_fields.get(name)
        if locked_fields is None or len(current_fields) > len(locked_fields):
            appended = True

    current_constants: dict[str, int] = {}
    for g in defs.constant_groups:
        for v in g.values:
            current_constants[f"{g.name}.{v.name}"] = v.id
    for key, locked_id in locked.constants.items():
        current_id = current_constants.get(key)
        if current_id is None:
            violations.append(
                f"constant '{key}' (id {locked_id}) is locked but is gone from "
                f"{SOURCE_NAME}. A constant value is never removed or renamed."
            )
        elif current_id != locked_id:
            violations.append(
                f"constant '{key}' is locked at id {locked_id} but {SOURCE_NAME} now "
                f"gives it id {current_id}. A constant value is never renumbered."
            )
    if set(current_constants) - set(locked.constants):
        appended = True

    current_functions = {fn.name: _fn_signature(fn) for fn in defs.functions}
    for name, locked_sig in locked.functions.items():
        current_sig = current_functions.get(name)
        if current_sig is None:
            violations.append(
                f"function '{name}' (locked signature {locked_sig}) is gone from "
                f"{SOURCE_NAME}. A function is never removed or re-signed - it can "
                f"only be superseded by a NEW function with a NEW name."
            )
        elif current_sig != locked_sig:
            violations.append(
                f"function '{name}' is locked with signature {locked_sig} but "
                f"{SOURCE_NAME} now gives it {current_sig}. A function's signature is "
                f"an ABI contract and is never changed once shipped; add a new "
                f"function instead."
            )
    if set(current_functions) - set(locked.functions):
        appended = True

    return violations, appended


def _fail(problems: list[str], summary: str) -> int:
    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
    print(f"error: {summary}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    default_root = Path(__file__).resolve().parent.parent

    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--check", action="store_true")
    action.add_argument("--write", action="store_true")
    parser.add_argument("--root", type=Path, default=default_root)
    parser.add_argument("--toml", type=Path, default=None)
    parser.add_argument("--lock", type=Path, default=None)
    args = parser.parse_args(argv)

    root = args.root.resolve()
    toml_path = (args.toml if args.toml is not None else root / TOML_RELPATH).resolve()
    lock_path = (args.lock if args.lock is not None else root / LOCK_RELPATH).resolve()
    props_toml_path = root / gen_props.TOML_RELPATH

    try:
        defs = load_definitions(toml_path, props_toml_path)
        locked = load_lock(lock_path, root)
    except GenError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if locked is None:
        if args.check:
            print(
                f"error: missing {_relative(lock_path, root)}; the ABI is unguarded. "
                f"Run `python3 {TOOL_NAME} --write` and commit it.",
                file=sys.stderr,
            )
            return 1
        locked = LockedState()

    violations, appended = compare(defs, locked)
    if violations:
        return _fail(
            violations,
            f"{SOURCE_NAME} disagrees with {LOCK_NAME} on {len(violations)} point(s). "
            f"Fix the definitions - do not edit {LOCK_NAME} to silence this.",
        )

    if args.check:
        if appended:
            return _fail(
                [
                    f"{SOURCE_NAME} has additions not yet recorded in {LOCK_NAME} "
                    f"(new function/struct field/constant/opaque type)."
                ],
                f"an append is live in the generated ABI but unguarded. Run "
                f"`python3 {TOOL_NAME} --write` and commit {LOCK_NAME} in the same "
                f"change as {SOURCE_NAME}.",
            )
        print(
            f"{len(locked.functions)} functions, {len(locked.struct_fields)} structs, "
            f"{len(locked.constants)} constants, {len(locked.opaque_types)} opaque types "
            f"locked; nothing changed"
        )
        return 0

    content = render_lock(defs)
    relative = _relative(lock_path, root)
    if lock_path.exists() and lock_path.read_text(encoding="utf-8") == content:
        print(f"unchanged: {relative}")
    else:
        lock_path.parent.mkdir(parents=True, exist_ok=True)
        lock_path.write_text(content, encoding="utf-8")
        print(f"wrote:     {relative}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
