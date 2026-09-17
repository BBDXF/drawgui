#!/usr/bin/env python3
"""Guard the action id ABI contract recorded in input/action_ids.lock.

Identical shape and identical reasoning to tools/prop_lock.py and
tools/theme_lock.py, applied to the fourth numeric-id family. tools/
gen_shortcuts.py answers "do the generated files match input/shortcuts.toml?";
this answers "has the source of truth broken its own contract?" -
renumbering `copy` from id 1 to id 5 regenerates happily and silently
breaks whatever 8-3d's ABI export bakes that id into.

  renumbered id      a locked name now carries a different id   FAIL
  renamed action     a locked id now carries a different name    FAIL
  deleted action     a locked entry is gone from the TOML        FAIL
  unrecorded append  a TOML action this lock has never seen      FAIL
  appended action    a fresh id and name, recorded by --write     ALLOWED

Usage:
  python3 tools/shortcut_lock.py --check
  python3 tools/shortcut_lock.py --write
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gen_shortcuts import (  # noqa: E402  (needs the sys.path line above)
    MAX_ACTION_ID,
    NAME_PATTERN,
    SOURCE_NAME,
    TOML_RELPATH,
    Action,
    Definitions,
    GenError,
    load_definitions,
)

TOOL_NAME = "tools/shortcut_lock.py"
LOCK_NAME = "input/action_ids.lock"
LOCK_RELPATH = Path("input") / "action_ids.lock"

ID_COLUMN = 5

LOCK_HEADER = f"""\
# ============================================================================
# drawgui shortcut action id lock - an ABI contract, not a cache.
#
# One line per locked action, "<id>  <name>", in ascending id order.
# Written by `python3 {TOOL_NAME} --write` and verified by
# `python3 {TOOL_NAME} --check`. Same contract as props/prop_ids.lock and
# themes/token_ids.lock, applied to the action_id family (design.md
# section 5.5.3).
#
# NEVER hand-edit this file to make a failing check pass. Fix
# {SOURCE_NAME} instead.
# ============================================================================
"""


@dataclass(frozen=True)
class LockEntry:
    id: int
    name: str


def render_lock(defs: Definitions) -> str:
    lines = [LOCK_HEADER.rstrip("\n")]
    lines += [f"{action.id:>{ID_COLUMN}}  {action.name}" for action in defs.actions]
    return "\n".join(lines) + "\n"


def parse_lock(text: str, where: str) -> tuple[LockEntry, ...]:
    entries: list[LockEntry] = []
    for lineno, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        at = f"{where}:{lineno}"
        fields = line.split()
        if len(fields) != 2:
            raise GenError(f"{at}: expected exactly '<id>  <name>', found {raw!r}")
        raw_id, name = fields
        if not raw_id.isdigit():
            raise GenError(f"{at}: '{raw_id}' is not an action id")
        action_id = int(raw_id)
        if action_id < 1 or action_id > MAX_ACTION_ID:
            raise GenError(f"{at}: id {action_id} is outside the uint16_t range 1..{MAX_ACTION_ID}")
        if not NAME_PATTERN.match(name):
            raise GenError(f"{at}: '{name}' is not a snake_case action name")
        for previous in entries:
            if previous.id == action_id:
                raise GenError(f"{at}: id {action_id} is already locked to '{previous.name}'")
            if previous.name == name:
                raise GenError(f"{at}: '{name}' is already locked at id {previous.id}")
        if entries and action_id < entries[-1].id:
            raise GenError(f"{at}: id {action_id} follows id {entries[-1].id} out of order")
        entries.append(LockEntry(id=action_id, name=name))

    if not entries:
        raise GenError(f"{where}: no locked actions found; the file is empty or corrupt")
    return tuple(entries)


def load_lock(lock_path: Path, root: Path) -> tuple[LockEntry, ...] | None:
    if not lock_path.exists():
        return None
    where = _relative(lock_path, root)
    return parse_lock(lock_path.read_text(encoding="utf-8"), where)


def _relative(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def _renumbered(locked: LockEntry, current: Action) -> str:
    return (
        f"action '{locked.name}' is locked at id {locked.id} but {SOURCE_NAME} "
        f"now gives it id {current.id}. Restore id {locked.id}; a new action "
        f"takes meta.next_id instead."
    )


def _renamed(locked: LockEntry, current: Action) -> str:
    return (
        f"id {locked.id} is locked to action '{locked.name}' but {SOURCE_NAME} "
        f"now names it '{current.name}'. Restore the name '{locked.name}'."
    )


def _removed(locked: LockEntry) -> str:
    return (
        f"action '{locked.name}' (id {locked.id}) is locked but has disappeared "
        f"from {SOURCE_NAME}. A locked action is never deleted."
    )


def _unrecorded(action: Action) -> str:
    return (
        f"action '{action.name}' (id {action.id}) is in {SOURCE_NAME} but not in "
        f"{LOCK_NAME}. Run `python3 {TOOL_NAME} --write` and commit "
        f"{LOCK_NAME} in the same change as {SOURCE_NAME}."
    )


def compare(defs: Definitions, lock: tuple[LockEntry, ...]) -> tuple[list[str], list[Action]]:
    by_name = {action.name: action for action in defs.actions}
    by_id = {action.id: action for action in defs.actions}

    violations: list[str] = []
    for locked in lock:
        current_by_name = by_name.get(locked.name)
        if current_by_name is not None and current_by_name.id == locked.id:
            continue
        if current_by_name is not None:
            violations.append(_renumbered(locked, current_by_name))
            continue
        current_by_id = by_id.get(locked.id)
        if current_by_id is not None:
            violations.append(_renamed(locked, current_by_id))
            continue
        violations.append(_removed(locked))

    locked_names = {entry.name for entry in lock}
    appended = [action for action in defs.actions if action.name not in locked_names]
    return violations, appended


def _fail(problems: list[str], summary: str) -> int:
    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
    print(f"error: {summary}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    default_root = Path(__file__).resolve().parent.parent

    parser = argparse.ArgumentParser(description=__doc__)
    action_group = parser.add_mutually_exclusive_group(required=True)
    action_group.add_argument("--check", action="store_true")
    action_group.add_argument("--write", action="store_true")
    parser.add_argument("--root", type=Path, default=default_root)
    parser.add_argument("--toml", type=Path, default=None)
    parser.add_argument("--lock", type=Path, default=None)
    args = parser.parse_args(argv)

    root = args.root.resolve()
    toml_path = (args.toml if args.toml is not None else root / TOML_RELPATH).resolve()
    lock_path = (args.lock if args.lock is not None else root / LOCK_RELPATH).resolve()

    try:
        defs = load_definitions(toml_path)
        lock = load_lock(lock_path, root)
    except GenError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if lock is None:
        if args.check:
            print(
                f"error: missing {_relative(lock_path, root)}; the action ids are "
                f"unguarded. Run `python3 {TOOL_NAME} --write` and commit it.",
                file=sys.stderr,
            )
            return 1
        lock = ()

    violations, appended = compare(defs, lock)
    if violations:
        return _fail(
            violations,
            f"{SOURCE_NAME} disagrees with {LOCK_NAME} on {len(violations)} action(s). "
            f"Fix the TOML - do not edit {LOCK_NAME} to silence this.",
        )

    if args.check:
        if appended:
            return _fail(
                [_unrecorded(action) for action in appended],
                f"{len(appended)} action(s) in {SOURCE_NAME} are live in the generated "
                f"header but unguarded.",
            )
        print(f"{len(lock)} locked actions; every id and name is unchanged")
        return 0

    content = render_lock(defs)
    relative = _relative(lock_path, root)
    if lock_path.exists() and lock_path.read_text(encoding="utf-8") == content:
        print(f"unchanged: {relative}")
    else:
        lock_path.parent.mkdir(parents=True, exist_ok=True)
        lock_path.write_text(content, encoding="utf-8")
        print(f"wrote:     {relative}")
    print(f"{len(defs.actions)} actions locked")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
