// Widgets: state attached to nodes that already exist.
//
// THERE IS NO WIDGET TREE. This is the architectural decision sub-step 3 was
// asked to make, and doc/widgets.md argues it in full; the short form is that
// Flutter's Widget and Element trees exist to make a declarative REBUILD cheap
// - throwaway configuration objects diffed against persistent elements - and
// this project has no rebuild to make cheap. Nodes are created and mutated
// imperatively, which is also what the eventual C ABI will do, so the middle
// layer would have nothing to do.
//
// What is here instead is a side table: one optional Widget per node index,
// parallel to the layout and render vectors that are already indexed the same
// way. It adds no node, no identity and no traversal. Widget identity IS
// NodeId, and that is sound because the node vectors are append-only - nothing
// is ever removed, so an index never shifts under a widget that is holding it.
// THE DAY REMOVAL ARRIVES that stops being true, and doc/widgets.md names the
// generation counter it will need.
//
// A widget owns a SUBTREE, not a node: a button is a box with a label inside
// it. So the pointer lands on some descendant and owner_of() climbs to the
// nearest ancestor that accepts pointer input. That climb is why a button's
// own label does not have to be made invisible to hit testing, and why a panel
// containing a button does not steal its clicks.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/interaction.h"

namespace dg {

// Four kinds, not an open set and not a class hierarchy.
//
// A fifth kind is a fifth enumerator and a fifth case in one switch, which the
// compiler will demand - the same argument LayoutKind makes a layer down. An
// abstract IWidget with a virtual paint() would be the twelve speculative
// platform headers again, in a costume: the base class would be shaped by the
// four widgets that exist rather than by the ones that would justify it.
enum class WidgetKind : std::uint8_t {
  // Holds children and takes part in layout. Not interactive: a click inside
  // a panel belongs to whatever is inside it, or to nothing.
  kPanel,

  // Static text. Not interactive, so a label inside a button is transparent to
  // hit testing without needing a flag saying so.
  kLabel,

  kButton,

  // Earns its place over a third button by carrying state that OUTLIVES the
  // click. Button, label and panel are all pure functions of the current
  // pointer state; a checkbox's appearance depends on the pointer AND on
  // everything that happened before, which is the case a design that stored
  // interaction state only in the Interaction machine would get wrong.
  kCheckbox,

  // A clipping node (NodeStyle::overflow) whose single child is measured
  // under an unbounded constraint (BoxStyle::scroll_axis) and whose position
  // relative to that child is runtime state (RenderTree::set_scroll_offset) -
  // three existing mechanisms composed, not a fourth one. Earns its place the
  // same way kCheckbox did: the offset outlives the wheel or drag event that
  // produced it, which the stateless Interaction machine cannot hold.
  kScrollView,
};

// One widget's whole state. A plain struct of plain fields, for the reason
// design.md section 5.15.3 gives: compact POD reachable by a switch, not a
// per-node property map.
struct Widget {
  WidgetKind kind = WidgetKind::kPanel;

  // The three fills an interactive widget cycles through. A panel or a label
  // never changes fill and leaves these alone.
  Color fill_normal;
  Color fill_hover;
  Color fill_pressed;

  // kCheckbox only: the child node whose fill is turned on when checked.
  // Default-constructed NodeId names the root, which is never an indicator, so
  // it doubles as "none" without a second field to disagree with.
  NodeId indicator;
  Color indicator_on;
  Color indicator_off;

  bool checked = false;

  // kScrollView only. `scroll_axis` mirrors BoxStyle::scroll_axis - the same
  // vocabulary, not a second one, so a caller cannot set the layout half to
  // vertical and the widget half to horizontal by mistake. `scroll_content`
  // is the single child laid out at its full, possibly viewport-exceeding
  // size; its `local_bounds()` is what `scroll_by()` clamps the offset
  // against. NOT the offset itself - RenderTree::scroll_offset() is the only
  // copy of that, on purpose (doc/scrolling.md section 2).
  ScrollAxis scroll_axis = ScrollAxis::kNone;
  NodeId scroll_content;
};

class WidgetSet {
 public:
  void attach(NodeId id, const Widget& widget);

  [[nodiscard]] bool has(NodeId id) const;
  [[nodiscard]] std::size_t count() const;
  [[nodiscard]] const Widget& at(NodeId id) const;

  [[nodiscard]] bool accepts_pointer(NodeId id) const;
  [[nodiscard]] bool is_checked(NodeId id) const;

  // Flips a checkbox and reports its new state. A no-op returning false for
  // every other kind, so a caller acting on InteractionChange::clicked does
  // not have to switch on the kind before asking.
  bool toggle(NodeId id);

  // The nearest ancestor-or-self of `id` that accepts pointer input, or
  // nothing when the climb reaches the root without finding one.
  [[nodiscard]] std::optional<NodeId> owner_of(const RenderTree& tree, NodeId id) const;

  // Hit testing and that climb, together: the one call an event loop makes.
  [[nodiscard]] std::optional<NodeId> widget_at(const RenderTree& tree, PixelPoint point) const;

  // The nearest ancestor-or-self of `id` that is a kScrollView, or nothing.
  // This is design.md section 5.16.2's "the scrollable ancestor under the
  // pointer" - a SEPARATE climb from owner_of(), not a shared one: a wheel
  // over a button inside a scrolling list must still find the list, which a
  // climb that stopped at the first interactive ancestor would miss.
  [[nodiscard]] std::optional<NodeId> scrollable_owner_of(const RenderTree& tree,
                                                          NodeId id) const;

  // Moves a kScrollView's children by (dx, dy), clamped so its
  // `scroll_content` child - laid out at its full, possibly
  // viewport-exceeding size - never scrolls past its own edges in either
  // direction. Only the axis `scroll_axis` names moves; the other delta is
  // ignored, which is what keeps a horizontal wheel nudge from also nudging a
  // vertical list.
  //
  // `viewport_content` is the box scrolling happens within. It is a parameter
  // rather than something this file derives, because doing so would need a
  // LayoutTree - content_bounds() is layout's, not the render tree's - and
  // WidgetSet depends on RenderTree alone, matching every other method here.
  //
  // Returns false, and changes nothing, when `id` does not name a
  // kScrollView or the clamped offset equals what RenderTree already has -
  // the "was this worth a repaint" signal a caller uses before touching the
  // screen.
  bool scroll_by(RenderTree& tree, NodeId id, const PixelRect& viewport_content, int dx,
                 int dy) const;

  // Writes the appearance `id` should have in `state`, and damages nothing
  // when that appearance is already on screen.
  //
  // The comparison is here rather than inside RenderTree::set_fill because
  // this is where the knowledge is: an interaction loop refreshes a widget
  // whenever its state is touched, and a checkbox toggled while hovered would
  // otherwise damage its box for a colour that did not change. Damage has to
  // be a function of what the user can see, not of how often the loop asks.
  void refresh(RenderTree& tree, NodeId id, PointerState state) const;

 private:
  // Null when `id` names no widget, including when it is past the end of the
  // table. Every accessor goes through these rather than testing has() and
  // then dereferencing: the two-step form is correct but leaves the check and
  // the access in different expressions, which is a shape neither a reader nor
  // clang-analyzer can follow.
  [[nodiscard]] const Widget* find(NodeId id) const;
  [[nodiscard]] Widget* find(NodeId id);

  std::vector<std::optional<Widget>> widgets_;
};

}  // namespace dg
