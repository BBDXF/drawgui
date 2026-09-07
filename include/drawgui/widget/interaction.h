// The hover / press / click state machine.
//
// IT TAKES WIDGETS, NOT POSITIONS. Every method here is handed the widget the
// pointer is over - already resolved by hit testing - rather than a coordinate
// to resolve for itself. That split is the single most useful decision in this
// file, for three reasons:
//
//   The machine becomes a pure function of its inputs, testable with no tree,
//   no surface and no window. The awkward cases - press, drag out, drag back,
//   release - are then written as five lines each instead of as a scene.
//
//   "Rapid motion that skips pixels" stops being a case at all. A pointer that
//   jumps from one side of the window to the other delivers one event naming
//   one widget, and nothing in here believes the pointer moved continuously to
//   get there. A machine that interpolated between positions would have to.
//
//   Hit testing gets tested exhaustively on its own, against a brute-force
//   ground truth, without any interaction state in the way.
//
// EVERY TRANSITION IS ATOMIC. One event returns one InteractionChange carrying
// BOTH the widget that lost hover and the one that gained it, so there is no
// instant at which neither is hovered or both are. A machine that reported a
// leave and an enter as two events would let a repaint land between them and
// flicker on every boundary crossing - which is the defect that makes a row of
// buttons feel broken without ever being wrong for longer than a frame.
//
// NOT HERE, DELIBERATELY: focus, keyboard activation, double-click, drag
// thresholds, gesture disambiguation and pointer capture across widgets.
// design.md section 5.16 wants a gesture arena eventually; an arena with one
// competitor is a data structure with no purpose, and it arrives with the
// second recognizer.

#pragma once

#include <optional>

#include "drawgui/render/render_tree.h"

namespace dg {

// What one pointer event changed, in the vocabulary a caller has to act on:
// which widgets need repainting, and whether anything was activated.
//
// Five independent optionals rather than an event kind plus a target, because
// one event genuinely changes several things at once - a release inside a
// widget ends a press AND fires a click AND may move hover - and flattening
// that into a single "what happened" would force the caller to reconstruct the
// rest.
struct InteractionChange {
  std::optional<NodeId> left;
  std::optional<NodeId> entered;

  // The widget that took a press, and the widget whose press ended. `released`
  // is set whether or not the press became a click, because either way that
  // widget has stopped looking pressed and has to be repainted.
  std::optional<NodeId> pressed;
  std::optional<NodeId> released;

  // Set only when a press and its release landed on the SAME widget. This is
  // the activation a caller acts on.
  std::optional<NodeId> clicked;

  [[nodiscard]] bool any() const {
    return left.has_value() || entered.has_value() || pressed.has_value() ||
           released.has_value() || clicked.has_value();
  }
};

// How one widget should currently be drawn.
struct PointerState {
  bool hovered = false;
  bool pressed = false;

  friend bool operator==(PointerState, PointerState) = default;
};

class Interaction {
 public:
  // The pointer is now over `target`, or over no widget at all.
  //
  // While a press is in flight this does NOT hover whatever it passes over.
  // The press holds an implicit grab: dragging off a held button and across
  // its neighbour must not light the neighbour up, because releasing there
  // activates nothing and a highlight would promise otherwise.
  InteractionChange moved_over(std::optional<NodeId> target);

  InteractionChange pressed_on(std::optional<NodeId> target);

  // Fires a click only when `target` is the widget that took the press.
  // Pressing a button, dragging away and releasing is how a user cancels, and
  // it is the case a naive implementation gets wrong by firing on whatever is
  // under the pointer at release time.
  InteractionChange released_on(std::optional<NodeId> target);

  // The pointer left the window. Hover clears; the press does NOT, so that
  // leaving the window and coming back still allows the release that activates
  // it - the same thing every desktop toolkit does. The held widget stops
  // being DRAWN pressed, because the pointer is no longer over it.
  InteractionChange left_window();

  [[nodiscard]] std::optional<NodeId> hovered() const { return hovered_; }

  // The widget holding the press, if any. Distinct from "drawn pressed":
  // a widget the pointer has been dragged off still holds the press and can
  // still be activated by dragging back.
  [[nodiscard]] std::optional<NodeId> holding() const { return holding_; }

  [[nodiscard]] PointerState state_of(NodeId id) const;

 private:
  void set_hover(std::optional<NodeId> target, InteractionChange& change);

  std::optional<NodeId> hovered_;
  std::optional<NodeId> holding_;
  bool holding_inside_ = false;
};

}  // namespace dg
