#!/usr/bin/env python3
"""Prove tools/shortcut_lock.py and tools/gen_shortcuts.py still reject what
they were written to reject.

Same motivation as tools/prop_lock_selftest.py, word for word: `--check`
against a tree that is already correct is exactly the input that cannot
tell a working check from one that always returns 0, and that is the
precise class of bug that hit prop_lock.py once already (commit ed02c68,
found and closed in c5ce347). This feeds both the lock AND the generator
deliberately broken tables on purpose. Every case below MUST be rejected
unless stated otherwise.

Two kinds of case, because two different tools own two different failure
classes:

  the LOCK (tools/shortcut_lock.py) - unrecorded append, renumbered id,
  renamed action, deleted action, and the "guard the guard" regression: an
  append recorded correctly, then the SAME freshly-recorded id renumbered.

  the GENERATOR (tools/gen_shortcuts.py) - a duplicate chord bound twice in
  one scope, and a missing/empty `consumer` field. Neither is an id-ABI
  question at all, so tools/shortcut_lock.py cannot see either one; both
  are load_definitions()'s own job, exercised here directly.

Everything runs on copies in a temporary directory; the committed TOML and
lock are read but never written.

Usage:
  python3 tools/shortcut_lock_selftest.py
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOCK_TOOL = ROOT / "tools" / "shortcut_lock.py"
TOML_SOURCE = ROOT / "input" / "shortcuts.toml"
LOCK_SOURCE = ROOT / "input" / "action_ids.lock"

sys.path.insert(0, str(ROOT / "tools"))
import gen_shortcuts  # noqa: E402  (needs the sys.path line above)

# Appended with an id past every assigned one, exactly as input/shortcuts.toml's
# own field reference tells a contributor to do it.
APPENDED = """
[[action]]
name = "selftest_probe"
id = {id}
scope = "app"
consumer = "tools/shortcut_lock_selftest.py; never committed"
binding = "Alt+X"
summary = "Added by tools/shortcut_lock_selftest.py; never committed."
"""


def run_lock(toml_path: Path, lock_path: Path, action: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [
            sys.executable,
            str(LOCK_TOOL),
            action,
            "--root",
            str(ROOT),
            "--toml",
            str(toml_path),
            "--lock",
            str(lock_path),
        ],
        capture_output=True,
        text=True,
        check=False,
    )


class Case:
    """One tampering scenario and the verdict it must produce."""

    def __init__(self, name: str, must_fail: bool):
        self.name = name
        self.must_fail = must_fail


def bump_next_id(text: str, value: int) -> str:
    return text.replace("next_id = 9", f"next_id = {value}")


def append_action(text: str, action_id: int) -> str:
    return bump_next_id(text, action_id + 1) + APPENDED.format(id=action_id)


def report(case: Case, failed: bool, detail: str, failures: list[str]) -> None:
    verdict = "REJECTED" if failed else "accepted"
    if failed != case.must_fail:
        wanted = "reject" if case.must_fail else "accept"
        failures.append(f"{case.name}: expected the check to {wanted} this, it did not")
        print(f"  FAIL  {case.name}: {verdict}")
        for line in detail.strip().splitlines()[:2]:
            print(f"        {line}")
        return
    print(f"  ok    {case.name}: {verdict}")


def check_lock_case(case: Case, toml_path: Path, lock_path: Path, failures: list[str]) -> None:
    result = run_lock(toml_path, lock_path, "--check")
    report(case, result.returncode != 0, result.stderr or result.stdout, failures)


def check_generator_case(case: Case, toml_path: Path, failures: list[str]) -> None:
    """Cases that are load_definitions()'s own job, not the lock's - a
    duplicate chord or a missing consumer is wrong the moment the TOML is
    read, independent of whether any id was ever locked.
    """
    try:
        gen_shortcuts.load_definitions(toml_path)
        report(case, False, "", failures)
    except gen_shortcuts.GenError as exc:
        report(case, True, str(exc), failures)


def main() -> int:
    original_toml = TOML_SOURCE.read_text(encoding="utf-8")
    failures: list[str] = []

    print("shortcut_lock.py / gen_shortcuts.py self-test: every case below must be")
    print("rejected unless stated otherwise")

    with tempfile.TemporaryDirectory() as raw_tmp:
        tmp = Path(raw_tmp)

        # The committed pair must pass, or every rejection below proves nothing.
        clean_toml = tmp / "clean.toml"
        clean_lock = tmp / "clean.lock"
        shutil.copyfile(TOML_SOURCE, clean_toml)
        shutil.copyfile(LOCK_SOURCE, clean_lock)
        check_lock_case(
            Case("the committed table and lock agree", must_fail=False),
            clean_toml, clean_lock, failures,
        )

        # 1. Appended but never recorded.
        unrecorded_toml = tmp / "unrecorded.toml"
        unrecorded_lock = tmp / "unrecorded.lock"
        unrecorded_toml.write_text(append_action(original_toml, 20), encoding="utf-8")
        shutil.copyfile(LOCK_SOURCE, unrecorded_lock)
        check_lock_case(
            Case("an append left unrecorded", must_fail=True),
            unrecorded_toml, unrecorded_lock, failures,
        )

        # 2. A renumber that keeps the file in ascending order, so the
        #    generator's own ordering rule cannot be what catches it.
        renumber_toml = tmp / "renumber.toml"
        renumber_lock = tmp / "renumber.lock"
        renumber_toml.write_text(
            bump_next_id(original_toml, 21).replace(
                'name = "scroll_to_end"\nid = 8', 'name = "scroll_to_end"\nid = 20'
            ),
            encoding="utf-8",
        )
        shutil.copyfile(LOCK_SOURCE, renumber_lock)
        check_lock_case(
            Case("a locked id renumbered, order preserved", must_fail=True),
            renumber_toml, renumber_lock, failures,
        )

        # 3. A rename: the id stays, the generated constant's spelling changes.
        rename_toml = tmp / "rename.toml"
        rename_lock = tmp / "rename.lock"
        rename_toml.write_text(
            original_toml.replace('name = "copy"', 'name = "duplicate"'), encoding="utf-8"
        )
        shutil.copyfile(LOCK_SOURCE, rename_lock)
        check_lock_case(
            Case("a locked action renamed", must_fail=True),
            rename_toml, rename_lock, failures,
        )

        # 4. A deletion. A retired id stays claimed forever.
        delete_toml = tmp / "delete.toml"
        delete_lock = tmp / "delete.lock"
        marker = '[[action]]\nname = "scroll_to_end"'
        delete_toml.write_text(original_toml[: original_toml.index(marker)], encoding="utf-8")
        shutil.copyfile(LOCK_SOURCE, delete_lock)
        check_lock_case(
            Case("a locked action deleted", must_fail=True),
            delete_toml, delete_lock, failures,
        )

        # 5. THE REGRESSION CASE (prop_lock_selftest.py's own history, re-run
        #    here too): append, record it properly with --write, then
        #    renumber the id that was just recorded. Re-run this first if
        #    shortcut_lock.py is ever refactored.
        regression_toml = tmp / "regression.toml"
        regression_lock = tmp / "regression.lock"
        regression_toml.write_text(append_action(original_toml, 20), encoding="utf-8")
        shutil.copyfile(LOCK_SOURCE, regression_lock)

        written = run_lock(regression_toml, regression_lock, "--write")
        if written.returncode != 0:
            failures.append("--write refused to record a legitimate append")
            print(f"  FAIL  --write refused a legitimate append (exit {written.returncode})")
        else:
            check_lock_case(
                Case("an append, once recorded", must_fail=False),
                regression_toml, regression_lock, failures,
            )
            regression_toml.write_text(
                append_action(original_toml, 20)
                .replace("id = 20", "id = 30")
                .replace("next_id = 21", "next_id = 31"),
                encoding="utf-8",
            )
            check_lock_case(
                Case("a freshly recorded id then renumbered", must_fail=True),
                regression_toml, regression_lock, failures,
            )

        # 6. Two actions bound to the same chord in the same scope -
        #    design.md section 7's check 1, gen_shortcuts.py's own job.
        duplicate_toml = tmp / "duplicate.toml"
        duplicate_toml.write_text(
            original_toml.replace('binding = "Mod+X"', 'binding = "Mod+C"'), encoding="utf-8"
        )
        check_generator_case(
            Case("duplicate chord bound twice in one scope", must_fail=True),
            duplicate_toml, failures,
        )

        # 7. A missing `consumer` field - the mandatory "no speculative
        #    vocabulary" guard this generator is deliberately stricter about
        #    than props/themes/abi.
        no_consumer_toml = tmp / "no_consumer.toml"
        no_consumer_toml.write_text(
            original_toml.replace(
                'consumer = "8-4 clipboard"\nbinding = "Mod+C"',
                'binding = "Mod+C"',
            ),
            encoding="utf-8",
        )
        check_generator_case(
            Case("missing consumer field", must_fail=True),
            no_consumer_toml, failures,
        )

        # 8. An empty `consumer` field - the same guard, the other way a
        #    contributor could try to satisfy it without naming anything.
        empty_consumer_toml = tmp / "empty_consumer.toml"
        empty_consumer_toml.write_text(
            original_toml.replace('consumer = "8-4 clipboard"\nbinding = "Mod+C"',
                                   'consumer = ""\nbinding = "Mod+C"'),
            encoding="utf-8",
        )
        check_generator_case(
            Case("empty consumer field", must_fail=True),
            empty_consumer_toml, failures,
        )

    if TOML_SOURCE.read_text(encoding="utf-8") != original_toml:
        failures.append("the self-test modified input/shortcuts.toml")

    if failures:
        print()
        for failure in failures:
            print(f"error: {failure}", file=sys.stderr)
        print(
            f"error: shortcut_lock.py/gen_shortcuts.py no longer reject "
            f"{len(failures)} thing(s) they must reject",
            file=sys.stderr,
        )
        return 1

    print("shortcut_lock.py/gen_shortcuts.py reject every tampering case and accept")
    print("a recorded append")
    return 0


if __name__ == "__main__":
    sys.exit(main())
