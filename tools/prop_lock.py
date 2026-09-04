#!/usr/bin/env python3
"""Guard the property id ABI contract recorded in props/prop_ids.lock.

tools/gen_props.py answers "do the generated files match the source of
truth?". It cannot answer "has the source of truth broken its own contract?",
because renumbering `width` from id 1 to id 7 regenerates perfectly happily
and silently breaks every already-compiled consumer.

props/prop_ids.lock is the committed name-to-id mapping. This script compares
it against props/drawgui.props.toml:

  renumbered id      a locked name now carries a different id   FAIL
  renamed property   a locked id now carries a different name   FAIL
  deleted property   a locked entry is gone from the TOML       FAIL
  appended property  a fresh id with a fresh name               ALLOWED

Appending is MINOR and must never be blocked; the other three are MAJOR
breaks (design.md section 5.8 decision 4: values are never reused, and only a
MAJOR version may break them).

Usage:
  python3 tools/prop_lock.py --check    verify the TOML against the lock
  python3 tools/prop_lock.py --write    record the current mapping; refuses
                                        when the change is a break

There is deliberately no flag that makes a failing --check pass. --write runs
the same comparison first, so it can only ever append. A guard with a
convenient bypass is not a guard.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gen_props import (  # noqa: E402  (needs the sys.path line above)
    MAX_PROP_ID,
    NAME_PATTERN,
    SOURCE_NAME,
    TOML_RELPATH,
    Definitions,
    GenError,
    Property,
    load_definitions,
)

TOOL_NAME = "tools/prop_lock.py"
LOCK_NAME = "props/prop_ids.lock"
LOCK_RELPATH = Path("props") / "prop_ids.lock"

# Wide enough for every uint16_t id, so appending never reflows an existing
# line: an ABI change is always exactly the lines that changed.
ID_COLUMN = 5

LOCK_HEADER = f"""\
# ============================================================================
# drawgui property id lock - an ABI contract, not a cache.
#
# One line per locked property, "<id>  <name>", in ascending id order.
# Written by `python3 {TOOL_NAME} --write` and verified by
# `python3 {TOOL_NAME} --check`.
#
# Every id and name below is frozen. Compiled consumers already hold these
# numbers, so renumbering an id, renaming a property or deleting one is a
# MAJOR version break (design.md section 5.8 decision 4: values are never
# reused, and only a MAJOR version may break them). Appending a new property
# with a fresh id and a fresh name is MINOR and is allowed.
#
# NEVER hand-edit this file to make a failing check pass. The check is
# reporting that {SOURCE_NAME} broke its own contract; editing the
# ledger does not repair the break, it only hides it from the next reviewer.
# Fix the TOML instead. A genuinely intended break is a reviewed MAJOR
# version change that says so out loud, never a quiet edit here.
# ============================================================================
"""


@dataclass(frozen=True)
class LockEntry:
    """One committed name-to-id binding."""

    id: int
    name: str


# --------------------------------------------------------------------------
# The lock file. Plain text on purpose: a mapping serialised as TOML or JSON
# can reorder or reindent without changing meaning, and a reviewer would then
# have to read the whole hunk to find the one line that matters.
# --------------------------------------------------------------------------


def render_lock(defs: Definitions) -> str:
    lines = [LOCK_HEADER.rstrip("\n")]
    lines += [f"{prop.id:>{ID_COLUMN}}  {prop.name}" for prop in defs.properties]
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
            raise GenError(f"{at}: '{raw_id}' is not a property id")
        prop_id = int(raw_id)
        if prop_id < 1 or prop_id > MAX_PROP_ID:
            raise GenError(f"{at}: id {prop_id} is outside the uint16_t range 1..{MAX_PROP_ID}")
        if not NAME_PATTERN.match(name):
            raise GenError(f"{at}: '{name}' is not a snake_case property name")
        for previous in entries:
            if previous.id == prop_id:
                raise GenError(f"{at}: id {prop_id} is already locked to '{previous.name}'")
            if previous.name == name:
                raise GenError(f"{at}: '{name}' is already locked at id {previous.id}")
        if entries and prop_id < entries[-1].id:
            raise GenError(
                f"{at}: id {prop_id} follows id {entries[-1].id}; the lock is "
                f"written in ascending id order so an append is one added line"
            )
        entries.append(LockEntry(id=prop_id, name=name))

    if not entries:
        raise GenError(f"{where}: no locked properties found; the file is empty or corrupt")
    return tuple(entries)


def load_lock(lock_path: Path, root: Path) -> tuple[LockEntry, ...] | None:
    """Read the lock, or return None when it does not exist yet."""
    if not lock_path.exists():
        return None
    where = _relative(lock_path, root)
    return parse_lock(lock_path.read_text(encoding="utf-8"), where)


def _relative(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


# --------------------------------------------------------------------------
# The comparison. Each violation names the property, the locked value, the
# new value and the way out - a message that only says "mismatch" leaves the
# reader to guess which of the three breaks they committed.
# --------------------------------------------------------------------------


def _renumbered(locked: LockEntry, current: Property) -> str:
    return (
        f"property '{locked.name}' is locked at id {locked.id} but {SOURCE_NAME} "
        f"now gives it id {current.id}. A property id is an ABI contract and is "
        f"never renumbered: every compiled consumer already holds {locked.id}. "
        f"Restore id {locked.id}; a new property takes meta.next_id instead."
    )


def _renamed(locked: LockEntry, current: Property) -> str:
    return (
        f"id {locked.id} is locked to property '{locked.name}' but {SOURCE_NAME} "
        f"now names it '{current.name}'. The name is the generated constant "
        f"DG_PROP_{locked.name.upper()}, so renaming it breaks every consumer "
        f"that spells it out. Restore the name '{locked.name}'; if you want a "
        f"differently named property, append one with a fresh id."
    )


def _removed(locked: LockEntry) -> str:
    return (
        f"property '{locked.name}' (id {locked.id}) is locked but has "
        f"disappeared from {SOURCE_NAME}. A locked property is never deleted - "
        f"its id stays claimed forever. Restore the entry; dropping it is a "
        f"MAJOR version break, not a cleanup."
    )


def compare(
    defs: Definitions, lock: tuple[LockEntry, ...]
) -> tuple[list[str], list[Property]]:
    """Return (violations, properties appended since the lock was written)."""
    by_name = {prop.name: prop for prop in defs.properties}
    by_id = {prop.id: prop for prop in defs.properties}

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
    appended = [prop for prop in defs.properties if prop.name not in locked_names]
    return violations, appended


def _report_appended(appended: list[Property]) -> None:
    for prop in appended:
        print(f"appended:  '{prop.name}' (id {prop.id}) - new, not locked yet")
    if appended:
        print(
            f"note: appending is MINOR and is allowed. Run "
            f"`python3 {TOOL_NAME} --write` and commit {LOCK_NAME} alongside "
            f"{SOURCE_NAME} so the new id is locked too."
        )


def _fail(violations: list[str]) -> int:
    for violation in violations:
        print(f"error: {violation}", file=sys.stderr)
    print(
        f"error: {SOURCE_NAME} disagrees with {LOCK_NAME} on "
        f"{len(violations)} propert{'y' if len(violations) == 1 else 'ies'}. "
        f"Fix the TOML - do not edit {LOCK_NAME} to silence this.",
        file=sys.stderr,
    )
    return 1


def main(argv: list[str]) -> int:
    default_root = Path(__file__).resolve().parent.parent

    parser = argparse.ArgumentParser(
        description=(
            "Verify that the property ids in the single source of truth still "
            "honour the committed ABI contract in props/prop_ids.lock."
        )
    )
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument(
        "--check",
        action="store_true",
        help="exit non-zero if an id was renumbered, a name changed or a property vanished",
    )
    action.add_argument(
        "--write",
        action="store_true",
        help="record the current mapping; refuses when the change is a break",
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
    parser.add_argument(
        "--lock",
        type=Path,
        default=None,
        help=f"path to the id lock (default: <root>/{LOCK_RELPATH.as_posix()})",
    )
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
                f"error: missing {_relative(lock_path, root)}; the property ids "
                f"are unguarded. Run `python3 {TOOL_NAME} --write` and commit it.",
                file=sys.stderr,
            )
            return 1
        lock = ()

    violations, appended = compare(defs, lock)
    if violations:
        return _fail(violations)

    if args.check:
        _report_appended(appended)
        print(f"{len(lock)} locked properties; every id and name is unchanged")
        return 0

    content = render_lock(defs)
    relative = _relative(lock_path, root)
    if lock_path.exists() and lock_path.read_text(encoding="utf-8") == content:
        print(f"unchanged: {relative}")
    else:
        lock_path.parent.mkdir(parents=True, exist_ok=True)
        lock_path.write_text(content, encoding="utf-8")
        print(f"wrote:     {relative}")
    print(f"{len(defs.properties)} properties locked")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
