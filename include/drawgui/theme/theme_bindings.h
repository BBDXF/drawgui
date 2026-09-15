// $token live references - the mechanism that lets a theme switch update
// every bound node WITHOUT rebuilding the widget tree (design.md section
// 5.7.2, and this slice's own acceptance bar: P3's light/dark runtime
// switch).
//
// THE REPRESENTATION, defended against this project's own precedent: a
// SIDE TABLE, `ThemeBindings`, indexed by NodeId - the SAME shape
// `WidgetSet` already is (include/drawgui/widget/widget_set.h: "a
// std::vector<std::optional<Widget>>... per-node state that OUTLIVES an
// event"). The two rejected alternatives and why:
//
//   A SENTINEL VALUE INSIDE NodeStyle/BoxStyle (a magic Color or int meaning
//   "resolve token N at paint time") was rejected for the identical reason
//   doc/scrolling.md and doc/list.md already give for scroll offset and
//   list state: every READER of NodeStyle (the painter, hit testing, damage)
//   would have to learn a second meaning for a field it already understands
//   as a concrete value, and a field is only the right shape when EVERY
//   reader needs to agree on ONE rule (doc/clipping.md section 6's
//   `overflow` argument, restated). A theme binding is not a fact the
//   painter needs to know about the SHAPE of a value; it is a fact about
//   WHERE a concrete value came from, which only the theme switch itself
//   ever needs to re-consult. So it belongs beside WidgetSet, not inside
//   NodeStyle.
//
//   A GENERATION-COUNTER HANDLE (AnimHandle{index, generation}, doc/
//   animation.md) was considered and rejected too, for the opposite of
//   the reason it was RIGHT for animations. doc/animation.md's own
//   argument: a generation counter exists because an animation SLOT's
//   count is unbounded over a session (many short-lived hovers each spend
//   a slot, so slots must be freed and reused, reopening ABA) while a
//   render NODE's count is not (WidgetSet's std::vector<std::optional<...>>
//   never reclaims an index, because nodes are never removed - see
//   doc/list.md section 1). A theme binding's lifetime is tied to a NODE's
//   lifetime one-to-one (a binding names "this node's this property"), and
//   nodes in this engine are append-only exactly like WidgetSet's own
//   entries - there is no reuse, so there is no ABA problem to guard
//   against, and a generation counter here would be solving a problem this
//   slice's own data does not have.
//
// RESOLUTION REUSES dg::set_prop() UNCHANGED. Binding a token does not
// invent a new invalidation rule: it looks up the theme's current value for
// the bound variant and writes it through the EXACT SAME `dg::set_prop()`
// door an ordinary literal write already uses (node_props.h). That is what
// makes "does a colour-token switch cost a relayout" and "does an
// integer-token switch cost a relayout" already-answered questions rather
// than new code to get right: node_props.h's own comment ("WHICH
// INVALIDATION A WRITE COSTS is decided by which struct the property lands
// in") applies here for free, because a resolved token write IS an
// ordinary property write as far as that rule is concerned. doc/theme.md
// section 5 measures both cases with LayoutStats.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "drawgui/props/node_props.h"
#include "drawgui/theme/theme.h"

namespace dg {

class LayoutTree;

// One node's binding of one property to one token. A node may bind several
// different properties (background_color to color.surface, border_color to
// color.border), so the side table is per-node a small vector of these
// rather than a single slot.
struct TokenBinding {
  dg_prop_id prop_id;
  dg_token_id token_id;

  friend bool operator==(const TokenBinding&, const TokenBinding&) = default;
};

// The side table itself. Append-only in the same sense WidgetSet is: a
// binding is never removed (this slice adds no "unbind" API, matching
// design.md's own "运行时新增 token" scope boundary - see doc/theme.md),
// only rebound - re-binding the same (node, prop_id) pair to a different
// token replaces the entry rather than accumulating a second one.
class ThemeBindings {
 public:
  // Records that `node`'s `prop_id` is now driven by `token_id`, replacing
  // any earlier binding for the same (node, prop_id) pair. Grows the
  // backing vector to cover `node` the same way WidgetSet::ensure() does.
  void bind(NodeId node, dg_prop_id prop_id, dg_token_id token_id);

  [[nodiscard]] std::optional<dg_token_id> token_for(NodeId node, dg_prop_id prop_id) const;

  // Every binding for `node`, in no particular order - for a test or a
  // diagnostic that wants to enumerate what one node is bound to.
  [[nodiscard]] std::vector<TokenBinding> bindings_for(NodeId node) const;

  // Re-resolves EVERY recorded binding against `theme`/`variant` and writes
  // the result through dg::set_prop() - the whole of a theme switch. Nodes
  // with no binding are untouched, exactly like an ordinary property write
  // that never happens. Returns how many bindings had no value in `theme`
  // for `variant` (a partial theme - see theme.h; tools/check_consistency.py
  // is what keeps the BUILTIN theme from ever producing a nonzero count
  // here, but a hand-authored or future external theme is not guaranteed
  // complete, and reporting the count rather than asserting is what lets a
  // caller notice without this function aborting mid-switch).
  std::size_t apply(LayoutTree& tree, const Theme& theme, ThemeVariant variant) const;

 private:
  std::vector<std::vector<TokenBinding>> by_node_;
};

// The dedicated binding entry point - the same id-validation shape as
// dg::set_gradient()/set_shadow()/set_image() (design.md section 5.9.5,
// node_props.h), extended one step: after validating, it resolves the
// token's CURRENT value (under `variant`) and performs the write through
// dg::set_prop() immediately, so binding a token has the same instant,
// observable effect an ordinary dg::set_prop() call already has - a caller
// never sees an unresolved or stale node between bind_token() returning and
// the next repaint.
//
// COMPATIBILITY, not a new type system: a `k_color` token may bind any
// `k_color` property (background_color, border_color, ...); a `k_int`
// token may bind any `k_float` or `k_length` property (gap, padding_*,
// border_radius_*, width/height, ...) - the same two scalar PropTypes
// dg::set_prop()'s to_pixels() already rounds a float into device pixels
// for. Anything else is kTypeMismatch, reported through the SAME PropWrite
// vocabulary set_prop()/the dedicated setters already use - deliberately
// NOT a new dg::Expected-based error type: the loader's unknown-token/
// type-mismatch failures (theme_loader.h) are ABOUT DATA (a theme.json's
// shape), decided once at load time, while bind_token()'s failures are
// ABOUT AN API CALL (a caller's prop_id/token_id pairing), decided per
// call exactly like every other property write in this project - reusing
// PropWrite here is reusing the established vocabulary for that shape of
// problem rather than inventing a second one beside it.
[[nodiscard]] PropWrite bind_token(LayoutTree& tree, ThemeBindings& bindings, NodeId node,
                                   dg_prop_id prop_id, const Theme& theme, ThemeVariant variant,
                                   dg_token_id token_id);

}  // namespace dg
