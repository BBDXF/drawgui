# Node removal: `{index, generation}`, `remove_child()`, six side tables

Slice 8-5, the last of this batch. `doc/widgets.md` recorded, years of
slices ago, exactly what removal would need the day it arrived - a
generation counter in `NodeId`, because tombstones-forever would leak
memory forever and compaction would silently reattach whole subtrees to
the wrong widget. This slice is that day: it implements the decision
`doc/widgets.md` already made, rather than inventing one now.

The short version:

- `NodeId` gains `{index, generation}`; the field `value` is renamed to
  `index` on purpose, so the compiler - not a human reviewer - finds every
  place that used to treat it as a raw vector subscript.
- `RenderTree::remove_child()`/`LayoutTree::remove_child()` tombstone a
  whole subtree, bump every tombstoned slot's generation, and never
  compact.
- `on_node_removed()` reaches **six** `NodeId`-keyed side tables, not five
  - `Interaction` was missing from the original cleanup list, found by
    reading the repo rather than trusting the list.
- The required-reference signature is measurably stronger than a
  convention: deleting a cleanup call inside it makes the parameter
  unused, and `-Werror=unused-parameter` turns that into a compile error.
- A tombstoned node's stale `parent` field defaults to `0`, which is also
  the root's own index - one guard in `RenderTree::parent()` closes that
  hole for all four ancestor walks at once.
- `AnimationEngine::cancel_all_for()` did not exist before this slice and
  had to be built for this call site.
- `doc/abi.md` section 5's Gap 3 is now closed; Gap 2 is not.

---

## 1. Why `{index, generation}`, not tombstones forever

`doc/widgets.md` lines 79-86 already worked this question through, before
any removal code existed: widget identity **is** `NodeId`, and that only
holds because the node vectors are append-only - nothing ever shifts an
index under a widget holding one. Removal breaks that unless one of two
things is true: tombstone the slot and never reclaim it (safe, but every
removed node leaks its slot forever), or give `NodeId` a generation
counter so a stale id is *detected* rather than silently reattached to
whatever the recycled index becomes next.

This slice picks the second option, because `doc/widgets.md` had already
picked it in advance - the header comment on the generation field cites
that document by name. There was no fresh design decision to make here;
the job was building the mechanism the earlier document had already
specified, and checking that it actually closes the hole it was written
to close.

## 2. Why the generation could not be packed into the existing `uint32_t`

`NodeId::value` was, before this slice, a plain `uint32_t` used directly
as a vector subscript in three places: `theme_bindings.cpp`,
`widget_set.cpp`, and `render_tree.cpp`. Packing a generation into the
high bits of that same integer (index in the low 24 bits, generation in
the high 8, say) was considered and declined: every one of those three
call sites would have kept compiling unchanged against a value that no
longer meant "the vector index," silently reading past the wrong slot the
first time a generation bit was ever set.

`NodeId` instead gained two real fields, `index` and `generation`, and
`value` was renamed to `index` rather than left in place with a second
field bolted on beside it. The rename is deliberate, not cosmetic: it
turns every one of those three raw-subscript call sites into a build
error the moment the field they read no longer exists under that name,
forcing a human to look at each one and decide explicitly whether it now
needs an `is_valid()` check before indexing - which is exactly the
question a bare rename should surface, and a silently-still-compiling
`uint32_t` bit-packing scheme would have hidden.

## 3. Six side tables, not five

The original architecture consultation for this slice named four
`NodeId`-keyed side tables to clean up on removal: `WidgetSet`,
`ThemeBindings`, `Focus`, and `ActionScopes`. Reading the actual header
files rather than trusting that list found two more:

- **`AnimationEngine`** (`animation_engine.h`) - which additionally had no
  per-node cancellation primitive to call yet at all. `cancel_all_for()`
  had to be written for this call site; it did not exist before this
  slice (section 5).
- **`Interaction`** (`interaction.h`) - the fifth table, and the one the
  original list missed entirely. `Interaction` holds `hovered_`/`holding_`,
  a single optional `NodeId` each, tracking which node the pointer is
  currently over and which node a mouse-down is currently pressed against.
  A stale `holding_` after a node is removed mid-drag means a drag in
  flight is being applied to a node that no longer exists - silent,
  because nothing about `Interaction`'s own interface would ever complain
  about holding a `NodeId` whose slot has since been tombstoned and reused.

Stating this plainly rather than glossing over it: the omission was found
by reading `interaction.h` directly, checking every `NodeId`-typed member
variable in the codebase against the cleanup list, not by re-trusting a
list that had already been wrong once. `node_lifecycle.h`'s own header
comment records both additions by name, so a future reader does not have
to re-derive which four were assumed and which two were found.

## 4. `on_node_removed()`'s required-reference signature, and what it actually guarantees

`on_node_removed()` (`src/render/node_lifecycle.h`) takes all six tables
as **required, non-defaulted reference parameters** - `WidgetSet&`,
`ThemeBindings&`, `Focus&`, `AnimationEngine&`, `ActionScopes&`,
`Interaction&` - rather than, say, six optional pointers a caller could
pass `nullptr` for, or a bundling struct with default-constructed members.

This is measurably stronger than it looks, not merely more explicit-
looking. `-Werror=unused-parameter` is enabled across this project's build,
and every one of the six tables is genuinely read inside the function
body (each gets its own `.forget()`/`cancel_all_for()` call). Deleting one
of those calls by hand - simulating "a future edit forgot to clean up
`Interaction`" - leaves that table's own parameter unused, which
`-Werror=unused-parameter` turns into a **compile error**, not a
review-visible diff a reviewer has to notice and reject. A seventh
`NodeId`-keyed side table arriving in some future slice either threads
through this same signature (forcing every existing call site to pass it,
the compiler doing the enforcement) or the future slice writes a second,
visibly separate cleanup function next to this one - which a reviewer
*can* see and reject on sight, unlike a silently-incomplete branch inside
a single "cleanup everything" function. Neither outcome lets a table
silently go uncleaned.

The honest limit of this guarantee, stated plainly rather than left
implicit: this protects against deleting a call to an *existing*
parameter. It does nothing for a seventh table someone adds as a *new*
member variable somewhere and simply never threads through this function
at all - that omission is still only review-visible, exactly as adding
any other new piece of state to this project always has been. The
compile-error guarantee is real, and it is also narrower than "every
future removal bug is now impossible."

## 5. The tombstone-as-root trap, and the one guard that closes it

`Node::parent` (the underlying storage field `RenderTree::parent()` reads)
defaults to `0` for a newly-tombstoned slot - and `0` is also the index of
the tree's own **root**. Left unguarded, a stale `parent` field on a
removed node would read back as "my parent is the root," which is exactly
wrong: a removed node has no parent at all, and an ancestor walk that
happens to pass through a tombstoned slot (holding a stale `NodeId` from
before it was removed, then calling `parent()` on it without checking
`is_valid()` first) would silently climb through a node that no longer
exists and land on the real root as if the stale id were genuinely
rooted there.

The fix is one guard, in one place: `RenderTree::parent(NodeId id)` checks
`is_valid(id)` first, and if it is not, returns `id` itself unchanged -
the exact "no parent to climb through, stop right here" sentinel this
project already uses everywhere else for the real root (`id == parent(id)`
is the established stopping condition every ancestor walk in this codebase
already relies on, per `parent()`'s own long-standing doc comment). Because
every ancestor walk in this project - Focus's scope-containment climb,
the shortcut router's level-2 bubble, hit testing's own ancestor checks,
and `dg_dump_layout_tree`'s traversal - already stops on that identical
condition, this one guard makes all four removal-safe at once, with no
change needed at any of the four call sites themselves.

## 6. `AnimationEngine::cancel_all_for()` did not exist

Before this slice, `AnimationEngine` had no per-node cancellation
primitive at all - nothing needed one, because nothing removed nodes.
`cancel_all_for(NodeId node)` was built specifically for this call site:
it walks the engine's own active-animation list and cancels every
animation targeting `node`, the identical shape `ThemeBindings::forget()`
and `ActionScopes::forget()` already had for their own tables. It is new
surface this slice adds, not a pre-existing method this slice merely
calls.

## 7. What was measured, not assumed

`test_node_removal.cpp` runs entirely in-process against `LayoutTree`/
`RenderTree` directly, no window or surface, the same precedent
`test_focus.cpp`/`test_shortcut_routing.cpp` already set. What it checks:

- Removing a leaf drops it from its parent's children and invalidates it;
  removing a subtree tombstones every descendant, not just its root, and
  `RenderTree::children()` on the former parent reflects exactly the
  removal.
- A stale `NodeId` (held from before removal) is rejected once its slot is
  reused by a later `add_child()` - the generation counter, not the raw
  index, is what a lookup actually checks.
- A tombstoned slot's `parent()` never masquerades as the root (section
  5), checked directly against `RenderTree::root()`.
- All six side tables are actually cleaned up: a real `Widget` attached,
  a real focused node, a real active `AnimHandle`, a real scoped action, a
  real theme binding, and real `hovered_`/`holding_` state are all set up
  first, `on_node_removed()` is called once, and every one of the six is
  checked gone afterward - not merely that the function returned `true`.
- **Damage equals the removed subtree's last absolute bounds**: after
  removing a subtree, the tree's own damage rectangle covers at least the
  union of the removed child's and grandchild's prior bounds - the
  established "old bounds ∪ new bounds" rule with new bounds empty, the
  same halfway shape `set_local_bounds()` already uses for an ordinary
  move.
- **`nodes_visited == 2`** after removing a child subtree and relaying out:
  exactly one dirty root (the former parent), visiting itself plus its one
  remaining live child (`sibling`) - not the whole tree, and not the
  removed subtree, which no longer exists to visit.
- Removing the root is refused (`on_node_removed()` returns `false`, and
  the root is still valid and still has its one child afterward) - a
  reported failure a caller can check, not a silent no-op that could be
  mistaken for success.
- Removing an already-invalid id is likewise refused, not a crash and not
  a silent success.

## 8. What this slice declines, by name

- **Insert-before-a-sibling.** `doc/abi.md`'s own Gap 2 (still open) names
  the same absence from the other direction: `add_child()` only ever
  appends, on both trees, because no caller anywhere in this project has
  ever needed to insert before an arbitrary sibling - every scene this
  project builds constructs its tree in one front-to-back pass. Building
  real removal did not, by itself, create a need for insertion order;
  nothing this slice added calls for it either. Prerequisite: a
  reorderable list (a caller that genuinely needs to insert a node
  between two existing siblings, not merely append or remove).
- **Removal inside an open popup.** `doc/popup.md` already records that
  closing an overlay popup does not remove its nodes at all - it clips the
  popup's container to an empty rectangle instead, leaving dead nodes in
  the tree permanently by design. This slice's `remove_child()` could
  technically be called on a popup's own subtree, but nothing in this
  project's popup-closing path does so, and interleaving real removal
  with a popup's own escape-hatch-node bookkeeping (`doc/popup.md` section
  3's own named restriction against relayout while such a popup is open)
  is a real, uninvestigated interaction this slice does not open.
  Prerequisite: a popup-closing path that actually wants nodes gone rather
  than merely invisible.
- **Removal mixed into `kList`'s recycled pool.** `doc/list.md` chose a
  fixed, permanent pool of interchangeable nodes over per-item node
  lifetime specifically to avoid ever needing removal for list
  virtualization - a pool node's content changes; its identity as a
  `NodeId` never does. Wiring real removal into that pool would undo the
  reason the pool exists in the first place. Prerequisite: a list-shaped
  caller that genuinely needs fewer live nodes than its own pool size,
  which nothing in this project currently does.

## 9. `doc/abi.md` section 5, updated

Gap 3 - "`dg_node_remove()` cannot detach a node's tree structure" - is
**closed** by this slice: `dg_node_remove()` now calls the real
`on_node_removed()` this slice built, so a removed ABI node genuinely
leaves its window's tree, is repainted around, and stops being laid out,
rather than merely having its handle invalidated while the underlying
node kept rendering exactly as before. Gap 2 - no insertion-order
primitive, only append - remains open, unaffected by this slice (section
8 above).
