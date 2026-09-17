# Development

## First thing after cloning

```sh
git config core.hooksPath .githooks
```

`.git/hooks/` is not tracked, so a hook cannot travel with a clone; this command
is what points git at the tracked `.githooks/` directory instead. It is one
command and it is easy to skip, which is precisely why the check it enables is
not the only one — see below.

`.githooks/pre-commit` refuses a commit whose author or committer is not this
repository's identity. It exists because a machine's global `user.email` is
inherited silently by any clone without a local one, and this project has
already paid for that once: an employer address reached 184 of 185 commits and
came out only by rewriting every one of them.

A client-side hook is a guardrail, not a control — `git commit --no-verify`
skips it, and someone who never ran the command above never had it. The check
that actually holds is `.github/workflows/ci.yml`'s `identity` job, which
re-reads the author and committer of every pushed commit and cannot be skipped
by the person making them.

Then install the pinned formatter, because CI enforces it:

```sh
pip install "clang-format==18.1.3"     # or: uv tool install "clang-format==18.1.3"
clang-format --style=file -i <files you touched>
```

**Install it from PyPI, not from your distribution, and take the exact patch
version.** Both halves matter, and neither is obvious:

- clang-format majors disagree on real constructs: 21 formats a
  pointer-to-member as `int C::* m`, 18 writes `int C::*m`.
- Patch releases disagree too: 18.1.3 keeps `<< "a" << "b"` on one line and
  18.1.8 breaks it. Pinning only `18` is not enough — that was tried, and CI
  still rejected files that passed locally.
- Your distribution cannot give you the right one anyway. ubuntu-24.04 (which
  CI runs) packages 18.1.3; newer Ubuntus package 18.1.8; apt offers no way to
  ask for the other. The PyPI wheel is the same binary everywhere, which is
  the only reason a contributor and CI can agree at all.

## Skia dependency

Skia is consumed as `BBDXF/libskia2` via `cmake/FetchSkia2.cmake` +
`find_package(skia2)`/`skia2::skia2` (P7 slice 7-1; superseded the old
rust-skia-based `cmake/FetchSkia.cmake`). `doc/skia-dependency.md` has the
full switch record - what was verified, the golden-image risk assessment,
the design.md §12/§5.10.5/§5.13.6 settlement, and the measurements.

**New system prerequisite: `libfontconfig1-dev`.** `skia2Config.cmake`
requires `libfontconfig` and its headers to configure the link on Linux
(Skia's own `BUILD.gn` never vendors fontconfig). This is a *build*-time
requirement only - `src/render/font_catalog.cpp` still never calls
`SkFontMgr_New_FontConfig`, so no drawgui binary carries a runtime
dependency on `libfontconfig` (verified with `ldd`; see `doc/skia-
dependency.md` section 6 and `doc/font-fallback.md`). On a fresh machine:

```sh
sudo apt-get install -y libfontconfig1-dev
```

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

   This appends one line to `props/prop_ids.lock`. It is not optional: step 2
   already made `DG_PROP_<NEW>` a real constant that a consumer can compile
   against, so until this line exists the new id is live and unguarded.
   `prop_lock.py --check` fails while it is missing.
4. Commit the TOML, both generated files and the lock **together**. Adding a
   property is a two-file source change - `props/drawgui.props.toml` and
   `props/prop_ids.lock` - and the two belong in one commit. A checkout must
   always pass `--check` on both tools.

## Adding a theme token

`themes/schema.toml` is the identical shape one level over, for `token_id`
instead of `prop_id` (doc/theme.md has the full reasoning for why this is a
second small tool rather than an extension of `gen_props.py`).

1. Append a `[[token]]` table at the end of the file, dotted name
   (`color.something`, `radius.something`), `id` from `meta.next_id`, bumped.
2. Regenerate:

   ```sh
   python3 tools/gen_theme.py
   ```

   This rewrites `include/drawgui/theme/token_ids.generated.h`,
   `src/theme/token_table.generated.inc` and `doc/theme-tokens.generated.md`.
3. Record the id:

   ```sh
   python3 tools/theme_lock.py --write
   ```

   Appends one line to `themes/token_ids.lock`; `--check` fails while it is
   missing, for the identical reason `prop_lock.py --check` does.
4. Add the new token's value(s) to `themes/builtin/theme.json` - an `int`
   token goes in `"base"`, a `color` token in both `"variants.light"` and
   `"variants.dark"` - then verify coverage:

   ```sh
   python3 tools/check_consistency.py
   ```

   This is the gate CI runs; a schema token the builtin theme does not
   supply a value for fails it.
5. Commit the schema TOML, the three generated files, the lock, the updated
   `theme.json` and `gen_theme.py`'s builtin-theme-embedding regeneration
   (`include/drawgui/theme/builtin_theme.generated.h`, rewritten by the same
   `python3 tools/gen_theme.py` call in step 2) together.

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

`prop_lock.py --check` fails on exactly four things:

- an id was **renumbered** - a locked name now carries a different id
- a property was **renamed** - a locked id now carries a different name
- a locked property was **deleted** - its id must stay claimed forever
- an append was **left unrecorded** - the TOML has a property the lock has
  never seen

The last one is not a restriction on appending. Appending is MINOR and is
always permitted; what is rejected is appending and then not writing it down.
`cmake --build build` regenerates the header from the TOML alone, so a new
property is compilable the instant it is added, while nothing guards its id
until it reaches the lock. A guard that only starts protecting an id once
somebody remembers to run `--write` has a lag window, and "depends on someone
remembering" is precisely the failure the stonegui retrospective records.
Running `--write` closes it in the same commit.

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
- unrecorded - run `python3 tools/prop_lock.py --write` and commit the lock
  with your TOML edit. This is the one case where the fix is to update the
  lock, and `--write` does it for you; you still never edit it by hand.

If a break is genuinely intended, it is a MAJOR version change and lands as a
reviewed commit that says so out loud - not as a quiet edit to the lock.

## Running a GUI example under WSL with GPU acceleration

`/dev/dri` is absent under WSL, which looks like "no GPU" but is not one - see
`doc/platform-notes.md` for the full measurement. WSL exposes the GPU through
`/dev/dxg` and Mesa's D3D12 driver, selected with an environment variable:

```sh
GALLIUM_DRIVER=d3d12 ./build/examples/<gui_example>
```

Without it, Mesa silently falls back to the `llvmpipe` software rasterizer.
There is no error and no warning - the example still runs, just on the CPU
path instead of the GPU path - so a benchmark or visual check run without this
variable is measuring the wrong thing without saying so.

## Running everything locally

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

`ctest` runs both property checks alongside the golden-image tests. CI runs
the same two commands directly in the `generated` job, so a stale or
contract-breaking commit fails before it reaches a build matrix.

## Running clang-tidy (local only - no longer a CI gate)

The `clang-tidy` CI job was removed; `doc/design.md` section 7.1 records that
decision, what it cost, and how to restore it properly. `.clang-tidy` itself
is still here, so the checks can be run on demand:

```sh
CC=clang CXX=clang++ cmake -B build-tidy -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tidy --target drawgui_props_generate
python3 - <<'PY' > /tmp/tidy-files.txt
import json, os
root = os.getcwd()
for e in json.load(open("build-tidy/compile_commands.json")):
    f = e["file"]
    if f.startswith(root) and "/third_party/" not in f and "generated" not in f:
        print(f)
PY
clang-tidy -p build-tidy $(cat /tmp/tidy-files.txt)
```

Deriving the file list from `compile_commands.json` like this is deliberate,
and is what the old CI job should have done: its hand-maintained list had
drifted to 187 entries against 195 first-party translation units, so eight
files were never linted by a gate that looked complete.

Expect findings. Nothing enforces this any more, so the tree is not
guaranteed clean against whichever clang-tidy version you have - and versions
disagree, which is the other half of why the job was removed.

## Running the sanitized suite

design.md section 7 puts ASan + UBSan over the test suite as part of
constraint C4. It is a separate build directory, not a flag on the normal one:

```sh
cmake -B build-san -G Ninja -DCMAKE_BUILD_TYPE=Debug -DDG_SANITIZE=ON
cmake --build build-san
ctest --test-dir build-san --output-on-failure
```

`DG_SANITIZE` defaults to OFF. The instrumented binaries are not what ships
and they run several times slower, so this is a deliberate extra run rather
than the normal one. CI has a dedicated `sanitize` job that does exactly the
above under both gcc and clang.

Debug is the recommended build type. UBSan's checks are emitted before
optimisation and an optimiser may delete the ones it can prove unreachable, so
`-O0` sees the most. A sanitized Release build is still useful and still
carries line numbers, because `DG_SANITIZE` adds `-g` regardless of
`CMAKE_BUILD_TYPE`.

The flags live on the `drawgui_sanitizers` interface target in the root
`CMakeLists.txt`, next to `drawgui_warnings`, and are applied to first-party
targets only. Skia is a prebuilt archive with no instrumentation; ASan is
designed to link against uninstrumented code, so this works, but it also means
a defect inside `libskia.a` is invisible here.

Both the compile line and the link line get the same flags, from one list. A
target compiled with `-fsanitize=address` but linked without it fails on
undefined `__asan_*` symbols, which reads as a mysterious linker error rather
than as the misconfiguration it is.

## Reading a sanitizer failure

`ctest` prints the report inline with `--output-on-failure`. There are two
shapes.

**ASan** names the fault kind on the first line, then gives two stack traces -
where the bad access happened, and where the memory it touched came from:

```
==33933==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x...
READ of size 1 at 0x... thread T0
    #0 ... in dg::RasterSurface::encode_png() const .../src/graphics/raster_surface.cpp:76
    #1 ... in render_scene .../tests/golden/golden_test.cpp:92
...
0x... is located 8 bytes after 756-byte region [0x...,0x...)
allocated by thread T0 here:
    #0 ... in operator new(unsigned long)
    #7 ... in dg::RasterSurface::encode_png() const .../src/graphics/raster_surface.cpp:73
```

Read the second trace first. "8 bytes after a 756-byte region allocated at
line 73" identifies the buffer; frame `#0` of the first trace identifies the
access. The shadow-byte dump underneath is rarely needed.

**UBSan** is one line naming the operation and the values, followed by a stack
trace:

```
.../src/graphics/raster_surface.cpp:67:17: runtime error: signed integer
overflow: 320 * 2147483647 cannot be represented in type 'int'
    #0 ... in dg::RasterSurface::encode_png() const .../raster_surface.cpp:67
    #1 ... in render_scene .../tests/golden/golden_test.cpp:92
```

The trace is there because `tests/CMakeLists.txt` sets
`UBSAN_OPTIONS=print_stacktrace=1` on the instrumented CTest entries. Without
it UBSan prints the first line and nothing else, which names the expression but
not the caller that fed it the bad value. Running a test binary by hand outside
`ctest` gets the bare version, so pass the variable yourself:

```sh
UBSAN_OPTIONS=print_stacktrace=1 ./build-san/tests/drawgui_unit_test
```

**A UBSan report always fails the test.** `-fno-sanitize-recover=undefined` is
in the flag set for exactly that reason. Without it UBSan prints the same
diagnostic, lets the program carry on, and the process still exits 0 - so CI
stays green over code that is undefined. Measured, on both compilers: the same
overflow exits 0 without the flag and 1 with it. design.md sections 5.4.7 and
5.17.5 both choose loud failure over quiet wrongness, and a sanitizer that only
warns is the opposite of that.

Two cosmetic differences between the compilers, neither of which changes the
verdict: clang appends a `SUMMARY:` line to UBSan reports and gcc does not, and
clang's traces carry a column number as well as a line.

## What the sanitizer configuration deliberately does and does not disable

One UBSan sub-check is off: **`vptr`**. It is off because it cannot work here,
not because it is noisy. `-fsanitize=vptr` emits a `typeinfo for T` reference
at every polymorphic dereference, and the prebuilt Skia archive is compiled
without RTTI, so a sanitized link fails outright with
`undefined reference to 'typeinfo for SkCanvas'`. First-party code declares no
virtual function, no `dynamic_cast` and no `typeid` anywhere - design.md
section 5.15.3 stores render-object state as plain fields - so the check has
nothing of ours to inspect. If drawgui ever grows a polymorphic type of its
own, this needs revisiting.

Nothing else is disabled, and in particular these two commonly-disabled things
are **left on**:

| Check | Status | Why |
| --- | --- | --- |
| `detect_leaks` (LeakSanitizer) | on | It is armed - verified against a deliberate leak - and currently reports nothing. Skia's lazily registered PNG decoder and its other process-lifetime state stay reachable at exit, so LSan is content. |
| `detect_container_overflow` | on | The usual reason to disable it is a `std::vector` crossing into non-instrumented code, which this project does do - `golden_image.cpp` hands `pixels.data()` straight to Skia. It was measured rather than assumed: libstdc++ 15 gates its container annotations behind `_GLIBCXX_SANITIZE_STD_ALLOCATOR`, which this build does not define, and a deliberate read into a vector's `[size, capacity)` slack went unreported under both compilers. |

If a future toolchain does annotate containers, the boundary crossing will
start producing false positives, and the fix is
`ASAN_OPTIONS=detect_container_overflow=0` added to the `ENVIRONMENT` property
already set on the instrumented tests in `tests/CMakeLists.txt` - not a
suppression file, and not turning ASan off.

There are no suppression files, because nothing needed suppressing. If one ever
becomes necessary it may name third-party paths only. Hiding a finding in
`src/`, `include/` or `tests/` behind a suppression defeats the point of
running this at all; the finding is the product.
