#!/usr/bin/env python3
"""Prove tools/prop_lock.py still rejects what it was written to reject.

The lock is what stops a property id being renumbered out from under an
already-compiled consumer. Its first cut (commit ed02c68) returned exit 0 for
an appended-but-unrecorded property, and then exit 0 again after that same new
id was renumbered - a live id guarded by nothing. The hole was found by hand
and closed in c5ce347.

Nothing has guarded the guard since. `props.abi_lock` runs --check against a
tree that is correct, and a correct tree is exactly the input that cannot tell
a working check from a check that returns 0 unconditionally. That is the same
trap this project has now hit twice: a validation step is invisible to
already-valid input, and hand-written tables are almost always already valid.

So this feeds it invalid input on purpose. Every case below MUST fail, and the
one that regressed before - append, record, then renumber the freshly recorded
id - is here by name.

Everything runs on copies in a temporary directory; the committed TOML and
lock are read but never written.

Usage:
  python3 tools/prop_lock_selftest.py
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOCK_TOOL = ROOT / "tools" / "prop_lock.py"
TOML_SOURCE = ROOT / "props" / "drawgui.props.toml"
LOCK_SOURCE = ROOT / "props" / "prop_ids.lock"

# Appended with an id past every assigned one, exactly as doc/development.md
# tells a contributor to do it.
APPENDED = """
[[property]]
name = "selftest_probe"
id = {id}
type = "float"
group = "visual"
field = "selftest_probe"
applies_to = ["box"]
summary = "Added by tools/prop_lock_selftest.py; never committed."
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
    """One tampering scenario and the exit status it must produce."""

    def __init__(self, name: str, must_fail: bool):
        self.name = name
        self.must_fail = must_fail


def bump_next_id(text: str, value: int) -> str:
    return text.replace("next_id = 50", f"next_id = {value}")


def append_property(text: str, prop_id: int) -> str:
    return bump_next_id(text, prop_id + 1) + APPENDED.format(id=prop_id)


def check_case(case: Case, toml_path: Path, lock_path: Path, failures: list[str]) -> None:
    result = run_lock(toml_path, lock_path, "--check")
    failed = result.returncode != 0
    verdict = "REJECTED" if failed else "accepted"
    if failed != case.must_fail:
        wanted = "reject" if case.must_fail else "accept"
        failures.append(f"{case.name}: expected the lock to {wanted} this, it did not")
        print(f"  FAIL  {case.name}: {verdict} (exit {result.returncode})")
        for line in (result.stderr or result.stdout).strip().splitlines()[:2]:
            print(f"        {line}")
        return
    print(f"  ok    {case.name}: {verdict} (exit {result.returncode})")


def main() -> int:
    original_toml = TOML_SOURCE.read_text(encoding="utf-8")
    failures: list[str] = []

    print("prop_lock.py self-test: every case below must be rejected unless stated")

    with tempfile.TemporaryDirectory() as raw_tmp:
        tmp = Path(raw_tmp)

        # The committed pair must pass, or every rejection below proves nothing.
        clean_toml = tmp / "clean.toml"
        clean_lock = tmp / "clean.lock"
        shutil.copyfile(TOML_SOURCE, clean_toml)
        shutil.copyfile(LOCK_SOURCE, clean_lock)
        check_case(Case("the committed table and lock agree", must_fail=False), clean_toml,
                   clean_lock, failures)

        # 1. Appended but never recorded. The generated header already defines
        #    the constant, so the id ships unguarded.
        unrecorded_toml = tmp / "unrecorded.toml"
        unrecorded_lock = tmp / "unrecorded.lock"
        unrecorded_toml.write_text(append_property(original_toml, 50), encoding="utf-8")
        shutil.copyfile(LOCK_SOURCE, unrecorded_lock)
        check_case(Case("an append left unrecorded", must_fail=True), unrecorded_toml,
                   unrecorded_lock, failures)

        # 2. A renumber that keeps the file in ascending order, so the
        #    generator's own ordering rule cannot be what catches it - the lock
        #    has to be.
        renumber_toml = tmp / "renumber.toml"
        renumber_lock = tmp / "renumber.lock"
        renumber_toml.write_text(
            bump_next_id(original_toml, 51).replace(
                'name = "bottom"\nid = 45', 'name = "bottom"\nid = 50'
            ),
            encoding="utf-8",
        )
        shutil.copyfile(LOCK_SOURCE, renumber_lock)
        check_case(Case("a locked id renumbered, order preserved", must_fail=True),
                   renumber_toml, renumber_lock, failures)

        # 3. A rename: the id stays, the generated constant's spelling changes.
        rename_toml = tmp / "rename.toml"
        rename_lock = tmp / "rename.lock"
        rename_toml.write_text(
            original_toml.replace('name = "opacity"', 'name = "alpha"'), encoding="utf-8"
        )
        shutil.copyfile(LOCK_SOURCE, rename_lock)
        check_case(Case("a locked property renamed", must_fail=True), rename_toml, rename_lock,
                   failures)

        # 4. A deletion. A retired id stays claimed forever.
        delete_toml = tmp / "delete.toml"
        delete_lock = tmp / "delete.lock"
        marker = '[[property]]\nname = "bottom"'
        delete_toml.write_text(original_toml[: original_toml.index(marker)], encoding="utf-8")
        shutil.copyfile(LOCK_SOURCE, delete_lock)
        check_case(Case("a locked property deleted", must_fail=True), delete_toml, delete_lock,
                   failures)

        # 5. THE REGRESSION CASE. Append, record it properly with --write, then
        #    renumber the id that was just recorded. The first cut of the tool
        #    returned 0 here, because the append had never entered the lock and
        #    so the renumber had nothing to contradict. Re-run this one first if
        #    prop_lock.py is ever refactored.
        regression_toml = tmp / "regression.toml"
        regression_lock = tmp / "regression.lock"
        regression_toml.write_text(append_property(original_toml, 50), encoding="utf-8")
        shutil.copyfile(LOCK_SOURCE, regression_lock)

        written = run_lock(regression_toml, regression_lock, "--write")
        if written.returncode != 0:
            failures.append("--write refused to record a legitimate append")
            print(f"  FAIL  --write refused a legitimate append (exit {written.returncode})")
        else:
            check_case(Case("an append, once recorded", must_fail=False), regression_toml,
                       regression_lock, failures)
            regression_toml.write_text(
                append_property(original_toml, 50)
                .replace("id = 50", "id = 60")
                .replace("next_id = 51", "next_id = 61"),
                encoding="utf-8",
            )
            check_case(Case("a freshly recorded id then renumbered", must_fail=True),
                       regression_toml, regression_lock, failures)

    if TOML_SOURCE.read_text(encoding="utf-8") != original_toml:
        failures.append("the self-test modified props/drawgui.props.toml")

    if failures:
        print()
        for failure in failures:
            print(f"error: {failure}", file=sys.stderr)
        print(
            f"error: prop_lock.py no longer rejects {len(failures)} thing(s) it must reject",
            file=sys.stderr,
        )
        return 1

    print("prop_lock.py rejects every tampering case and accepts a recorded append")
    return 0


if __name__ == "__main__":
    sys.exit(main())
