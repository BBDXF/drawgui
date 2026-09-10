// Which widget, if any, currently receives keyboard events - the missing
// concept doc/widgets.md section 9 and doc/scrolling.md section 1 both
// already named as absent from this engine.
//
// A SEPARATE class from dg::Interaction and from WidgetSet, on purpose,
// mirroring the split Interaction's own header argues for hover/press: focus
// is state that outlives a single event (a click), is a pure function of
// "which widget was last given it", and is testable with no tree and no
// window. Folding it into WidgetSet would make every widget carry a
// `focused` bool nothing but a TextField uses today - the same "one field
// nothing reads yet" shape this project has already declined once (kSlider's
// hover/press glow, doc/form-controls.md section 8).
//
// ONE FOCUSED WIDGET AT A TIME, the simplest model that satisfies this
// slice: clicking a focusable widget focuses it and blurs whatever was
// focused before; clicking anything else (or nothing) blurs it. No tab
// order, no focus TREE (design.md section 5.2's per-window FocusManager is
// P4 scope, not this slice's) - a single optional NodeId is the whole of
// what one TextField in one demo needs, and doc/widgets.md's own rule
// applies again here: an interface is extracted from a working
// implementation, not written ahead of one.

#pragma once

#include <optional>

#include "drawgui/render/render_tree.h"

namespace dg {

// What one focus change touched - the same atomic-transition shape
// InteractionChange already has, for the identical reason: a caller must
// never observe an instant where two widgets are focused, or where the one
// that lost focus has not yet been told to repaint itself unfocused.
struct FocusChange {
  std::optional<NodeId> blurred;
  std::optional<NodeId> focused;

  [[nodiscard]] bool any() const { return blurred.has_value() || focused.has_value(); }
};

class Focus {
 public:
  // Focuses `target`. Passing std::nullopt blurs whatever is focused without
  // focusing anything new - clicking empty space or a non-focusable widget.
  // Focusing the widget that is already focused reports no change, matching
  // Interaction's identical no-op shape for a redundant hover.
  FocusChange set(std::optional<NodeId> target);

  [[nodiscard]] std::optional<NodeId> current() const { return focused_; }
  [[nodiscard]] bool is_focused(NodeId id) const {
    return focused_.has_value() && *focused_ == id;
  }

 private:
  std::optional<NodeId> focused_;
};

}  // namespace dg
