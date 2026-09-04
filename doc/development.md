# Development

## Adding a property

Every layout and visual property lives in `props/drawgui.props.toml`, the
single source of truth (design.md section 5.8 decision 5). Nothing that knows
about a property is hand-written anywhere else.

1. Append a `[[property]]` table at the **end** of the file. Take the id from
   `meta.next_id` and bump `meta.next_id` by one. Entries are listed in
   ascending id order, so appending is always a diff at the bottom.
2. Regenerate the C++:

   ```sh
   python3 tools/gen_props.py
   ```

   This rewrites `include/drawgui/render/prop_ids.generated.h` and
   `src/render/prop_dispatch.generated.inc`. Never edit those two by hand.
3. Record the new id in the ABI ledger:

   ```sh
   python3 tools/prop_lock.py --write
   ```

   This appends one line to `props/prop_ids.lock`.
4. Commit the TOML, both generated files and the lock **together**. A
   checkout must always pass `--check` on both tools.

## Why ids are never reused

A `prop_id` is a `uint16_t` that crosses the C ABI. Consumers compile it into
their binaries, so the numbers are a contract: append-only, never reused,
never renumbered. Only a MAJOR version may break them (design.md section 5.8
decision 4).

Two checks guard this, and they answer different questions:

| Check | Question | Command |
| --- | --- | --- |
| `props.no_drift` | Do the generated files match the TOML? | `python3 tools/gen_props.py --check` |
| `props.abi_lock` | Does the TOML still honour the ids it already published? | `python3 tools/prop_lock.py --check` |

The first one is not enough on its own. Move `width` from id 1 to id 7 and
`gen_props.py` regenerates cheerfully; every already-compiled consumer then
writes the wrong field. `props/prop_ids.lock` holds the committed name-to-id
mapping so that the second question has an answer.

`prop_lock.py --check` fails on exactly three things:

- an id was **renumbered** - a locked name now carries a different id
- a property was **renamed** - a locked id now carries a different name
- a locked property was **deleted** - its id must stay claimed forever

Appending a brand-new property with a fresh id and a fresh name is a MINOR
change and passes; the check just reminds you to run `--write`.

## When the lock check fails

**Do not edit `props/prop_ids.lock`.** The check is not complaining about the
lock, it is reporting that `props/drawgui.props.toml` broke its own contract.
Editing the ledger does not repair the break, it only hides it from the next
reviewer, and `prop_lock.py --write` refuses to do it for you.

The error names the property, the locked value and the new value. Fix the
TOML:

- renumbered - restore the locked id. A new property takes `meta.next_id`.
- renamed - restore the locked name. If you want a differently named
  property, append a new one; the old id keeps its old name.
- deleted - restore the entry. Retiring a property is not a cleanup.

If a break is genuinely intended, it is a MAJOR version change and lands as a
reviewed commit that says so out loud - not as a quiet edit to the lock.

## Running everything locally

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

`ctest` runs both property checks alongside the golden-image tests. CI runs
the same two commands directly in the `generated` job, so a stale or
contract-breaking commit fails before it reaches a build matrix.
