#!/usr/bin/env python3
"""Verify the builtin theme covers every token themes/schema.toml declares.

design.md section 5.7.7 (C4 - consistency check): the schema is the single
source of truth, and this is the CI gate that a hand-authored theme.json has
not silently drifted from it - stonegui's four-file token drift, reborn as
"the schema grew a token and nobody's theme.json got it", is exactly the
failure mode this catches.

Two directions are both checked, because either alone misses half the bug:

  MISSING  a schema token absent from the builtin theme's base/variants -
           dg::load_theme() would report this at runtime (design.md section
           5.7.5's "unknown token name" rule reads the other direction too:
           a theme MUST supply every token the schema promises exists), but
           catching it at CI time is cheaper than at first run.
  UNKNOWN  a theme.json key that names no schema token at all - the loader
           itself rejects this too (it is the direction 5.7.5 states
           explicitly: "未知 token 名 -> 加载失败"), checked here again so a
           CI failure names the exact bad key without needing a build first.

This is deliberately a SEPARATE, small, dependency-free script rather than a
mode of tools/gen_theme.py: it reasons about DATA (a theme.json instance)
against a SCHEMA (compile-time contract) - the exact two-sides-of-one-line
distinction design.md section 5.7.1 draws - and keeping them in different
files is what keeps that line visible rather than blurred into one script
that does both jobs.

Usage:
  python3 tools/check_consistency.py
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gen_theme import BUILTIN_JSON_RELPATH, TOML_RELPATH, GenError, load_definitions  # noqa: E402


def main(argv: list[str]) -> int:
    del argv
    root = Path(__file__).resolve().parent.parent
    toml_path = root / TOML_RELPATH
    json_path = root / BUILTIN_JSON_RELPATH

    try:
        defs = load_definitions(toml_path)
    except GenError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    try:
        theme = json.loads(json_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"error: {json_path}: {exc}", file=sys.stderr)
        return 2

    base = theme.get("base", {})
    variants = theme.get("variants", {})
    if not isinstance(variants, dict) or not variants:
        print(f"error: {json_path}: 'variants' must be a non-empty object", file=sys.stderr)
        return 2

    problems: list[str] = []

    int_tokens = {tok.name for tok in defs.tokens if tok.type == "int"}
    color_tokens = {tok.name for tok in defs.tokens if tok.type == "color"}

    missing_int = sorted(int_tokens - set(base))
    for name in missing_int:
        problems.append(f"schema token '{name}' (type int) is missing from theme.json's 'base'")

    unknown_base = sorted(set(base) - int_tokens)
    for name in unknown_base:
        problems.append(f"theme.json's 'base' names '{name}', which is not a schema int token")

    for variant_name, values in sorted(variants.items()):
        if not isinstance(values, dict):
            problems.append(f"variant '{variant_name}' must be an object")
            continue
        missing_color = sorted(color_tokens - set(values))
        for name in missing_color:
            problems.append(
                f"schema token '{name}' (type color) is missing from theme.json's "
                f"variant '{variant_name}'"
            )
        unknown_color = sorted(set(values) - color_tokens)
        for name in unknown_color:
            problems.append(
                f"theme.json's variant '{variant_name}' names '{name}', which is not "
                f"a schema color token"
            )

    if problems:
        for problem in problems:
            print(f"error: {problem}", file=sys.stderr)
        print(
            f"error: {json_path.relative_to(root)} disagrees with "
            f"{toml_path.relative_to(root)} on {len(problems)} token(s)",
            file=sys.stderr,
        )
        return 1

    print(
        f"{len(defs.tokens)} schema tokens; {json_path.relative_to(root)} covers all of "
        f"them across {len(variants)} variant(s) ({', '.join(sorted(variants))})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
