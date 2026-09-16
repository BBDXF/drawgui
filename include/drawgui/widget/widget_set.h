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
#include <string>
#include <string_view>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/render/font_catalog.h"
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

  // A track (this node, a kLeaf) and a thumb (its one child), whose position
  // along the track is a function of `value` - PAINT-time positioning via
  // RenderTree::set_local_origin, the same primitive a scroll offset already
  // proved out, not a new RenderObject kind (design.md section 5.6 line 622's
  // acceptance bar for exactly this control). Earns its place over reusing
  // kCheckbox the way kScrollView did: dragging accumulates a value across an
  // unbounded stream of pointer deltas, which the stateless Interaction
  // machine cannot hold - doc/form-controls.md section 2.
  kSlider,

  // A single-line text field, editable in whole grapheme clusters (7-2b) -
  // 4-9 originally scoped this to printable ASCII, a restriction 7-2b lifted
  // (doc/text-input.md's cross-reference to doc/text-layout.md section 2).
  // Composition, not a new primitive: `content` (a plain text-bearing
  // child), `caret`, `selection_highlight` and `composition_underline`
  // (plain fill-only children the widget positions) are exactly the shape
  // kCheckbox's `indicator` and kSlider's `thumb` already are, and the
  // field's own node is a kLeaf with `overflow: kClip` - the clip
  // doc/clipping.md built, reused verbatim to confine all four children,
  // the same way doc/scrolling.md reused it for a viewport. Earns a kind of
  // its own over reusing kSlider or kCheckbox because it needs FOCUS
  // (keyboard routing) and a MODEL string, neither of which any existing
  // kind carries - doc/text-input.md section 3. IME composition (7-3,
  // doc/ime.md) needed no fifth child kind and no new WidgetKind either -
  // just one more positioned plain child and more runtime state, the same
  // shape every extension to this control has taken so far.
  kTextField,

  // A virtualized, fixed-extent list: a clipping node (NodeStyle::overflow,
  // reused verbatim) whose children are a small, PERMANENT pool of item
  // nodes - never one node per logical item. `list_pool.size()` is bounded
  // by the VIEWPORT, not by `list_item_count`, and stays that way at 1000
  // items exactly as it is at 10; the recycling doc/list.md section 2
  // argues for is what makes that true. Earns a kind of its own over
  // extending kScrollView because it needs RUNTIME BOOKKEEPING no existing
  // kind carries (`list_assigned` - which logical index each pool slot
  // currently shows) and because a kScrollView's `scroll_content` is one
  // already-measured child, which is exactly the thing a virtualized list
  // cannot have (doc/list.md section 1).
  kList,

  // An anchor (this node, interactive like kButton) plus a caller-declared
  // list of option strings and a selected index that OUTLIVES the click
  // that set it - the same "state a checkbox's checked or a slider's value
  // already earns a kind over" argument doc/form-controls.md section 1.2
  // made for kSlider, one control over: kButton alone has nowhere to keep
  // "which option is selected" once the popup that showed the choice has
  // closed. The open/closed popup itself is NOT part of this state -
  // PopupHost's own handle already owns that, matching every other popup
  // client (doc/popup.md) - and the option ROWS are ordinary kButton
  // widgets built fresh each time the popup opens (doc/menus.md), not a
  // second array of NodeIds this kind tracks itself. Keyboard highlight
  // navigation while the popup is open reuses dg::Focus::focus_next()/
  // focus_previous() over the popup's own scope verbatim - the identical
  // traversal Tab already performs (7-4), routed by Up/Down instead -
  // so no highlight-cursor field belongs here either.
  kDropdown,
};

// One widget's whole state. A plain struct of plain fields, for the reason
// design.md section 5.15.3 gives: compact POD reachable by a switch, not a
// per-node property map.
struct Widget {
  WidgetKind kind = WidgetKind::kPanel;

  // 7-4's Tab-order override (dg::focus_order(), include/drawgui/widget/
  // focus.h), a construction-time declarative field for the identical
  // reason `min_value`/`max_value`/`step` already are one struct up:
  // nothing consumes it through dg::set_prop() (design.md names
  // `tab_index` in section 5.9.6 as an intended property, but it was never
  // added to props/drawgui.props.toml - there is no generated prop_id for
  // it to bind to yet), so it stays a plain field until a caller needs the
  // C ABI to set it. std::nullopt (the default) means "plain tree order";
  // a POSITIVE value is HTML's own tabindex convention - visited ascending
  // before every unset/zero widget; a NEGATIVE value removes this widget
  // from Tab/Shift-Tab's own sequence entirely without making it
  // unfocusable (a direct click, or dg::Focus::set(), still reaches it).
  std::optional<int> tab_index;

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

  // kCheckbox only. Unset: an ordinary checkbox, and `toggle()` flips
  // `checked` exactly as it always has. Set: a RADIO BUTTON - clicking it
  // always SELECTS it (an already-checked one is a no-op, not a toggle-off,
  // matching every desktop toolkit) and clears `checked` on every OTHER
  // kCheckbox sharing the same group id. This is the entire feature: a radio
  // is "checkbox plus one field", not a new WidgetKind, the same way
  // doc/scrolling.md found scrolling was three existing mechanisms composed
  // rather than a fourth one - doc/form-controls.md section 1.
  std::optional<int> group;

  // kScrollView only. `scroll_axis` mirrors BoxStyle::scroll_axis - the same
  // vocabulary, not a second one, so a caller cannot set the layout half to
  // vertical and the widget half to horizontal by mistake. `scroll_content`
  // is the single child laid out at its full, possibly viewport-exceeding
  // size; its `local_bounds()` is what `scroll_by()` clamps the offset
  // against. NOT the offset itself - RenderTree::scroll_offset() is the only
  // copy of that, on purpose (doc/scrolling.md section 2).
  ScrollAxis scroll_axis = ScrollAxis::kNone;
  NodeId scroll_content;

  // kSlider only. `thumb` is the single child node this widget MOVES rather
  // than paints - the same shape kCheckbox's `indicator` already has, one
  // level over. `min_value`/`max_value` bound `value`; `step`, when
  // positive, snaps it to the nearest multiple above `min_value` (0 means
  // continuous).
  //
  // `value` is RUNTIME STATE, not a property, for the identical reason
  // RenderTree's scroll offset is (doc/scrolling.md section 2): it
  // accumulates across an unbounded stream of drag deltas rather than being
  // declared once, so putting it in props/drawgui.props.toml would turn
  // every drag pixel into a dg_node_set_prop call across the eventual C
  // ABI - the anti-pattern design.md section 5.15.3 already rejected.
  // `min_value`/`max_value`/`step` ARE declarative and long-lived, and would
  // be the natural property candidates the day a caller sets them through
  // the C ABI; they stay plain construction-time fields here because nothing
  // consumes them through set_prop() yet - doc/properties.md's own standard
  // for every property already implemented: a representation is not added
  // before the code that consumes it exists.
  NodeId thumb;
  float min_value = 0.0F;
  float max_value = 1.0F;
  float step = 0.0F;
  float value = 0.0F;

  // kTextField only. `content`, `caret` and `selection_highlight` are plain
  // children this widget positions/sizes at paint time - the same shape
  // `indicator` and `thumb` already are above. `content`'s OWN
  // NodeStyle::text.text is a PAINT-TIME PROJECTION of `text` below, not a
  // second copy of it: it may show an ellipsis-truncated prefix while
  // unfocused, or the full string scrolled by `scroll_x` while focused - the
  // same relationship `checked`/the indicator's fill and `value`/the thumb's
  // position already have. doc/text-input.md section 3 is the argument.
  //
  // `composition_underline` is 7-3's own addition (doc/ime.md) - a fourth
  // plain positioned child, same shape as the three above it: a thin bar
  // under the IME's in-progress preedit span, the conventional visual
  // distinction between composing and committed text. No new RenderObject
  // or paint primitive, matching every other child here.
  NodeId content;
  NodeId caret;
  NodeId selection_highlight;
  NodeId composition_underline;

  // The MODEL. RUNTIME STATE, not a property - the identical argument
  // doc/scrolling.md section 2 and doc/form-controls.md section 1.3 already
  // make for the scroll offset and the slider's value: this accumulates
  // across an unbounded stream of keystrokes rather than being declared
  // once. Well-formed UTF-8, sanitized at every mutation entry point
  // (WidgetSet::text_field_insert()); `cursor`/`selection_anchor` are byte
  // offsets that ALWAYS land on a grapheme-cluster boundary (dg::
  // grapheme_boundaries()), enforced by construction because every mutator
  // only ever places them via a grapheme-boundary query, never raw
  // arithmetic - 7-2b lifted 4-9's ASCII-only restriction (doc/text-
  // input.md's cross-reference to doc/text-layout.md section 2) once
  // libgrapheme's segmentation (7-1) made real grapheme-cluster boundaries
  // available; design.md's mandatory minimum edit unit (line ~1001) is now
  // honoured by that segmentation rather than by ASCII's byte-offset
  // coincidence.
  std::string text;
  int cursor = 0;                       // byte offset into `text`, in [0, text.size()]
  std::optional<int> selection_anchor;  // set => a selection [min(anchor,cursor), max(...))
  int scroll_x = 0;                     // device pixels; DERIVED, never declared

  // 7-3's own composition state (doc/ime.md), RUNTIME STATE for the
  // identical reason `text`/`cursor` above already are - SDL delivers one
  // SDL_EVENT_TEXT_EDITING per keystroke while an IME composes, an
  // unbounded stream exactly like committed keystrokes. Deliberately
  // SEPARATE from `text`/`cursor`/`selection_anchor` rather than folded
  // into them: composition never touches any of the three above until a
  // REAL commit arrives (a TextInputEvent, through text_field_insert()
  // unchanged) - this is what lets Escape/focus-loss/a click mid-
  // composition simply discard `composition_text` below and leave the
  // committed model exactly as it was, with no undo to perform.
  //
  // `composing` is false whenever nothing is in progress; while true,
  // `composition_replace_start`/`composition_replace_end` are the byte
  // range WITHIN `text` composition virtually sits over (captured ONCE at
  // composition start from whatever selection/cursor was there - a plain
  // cursor position with no selection is `{cursor, cursor}`), and
  // `composition_text` is the IME's own not-yet-committed string (already
  // sanitized). `composition_focus_start`/`composition_focus_length` are
  // byte offsets WITHIN `composition_text` - SDL_TextEditingEvent's own
  // `start`/`length` (TextEditingEvent's own comment records the "UTF-8
  // characters" unit question this slice found and could not verify
  // against a live IME), converted to bytes and clamped defensively.
  bool composing = false;
  std::string composition_text;
  int composition_focus_start = 0;
  int composition_focus_length = 0;
  int composition_replace_start = 0;
  int composition_replace_end = 0;

  // kList only. `list_pool` is the PERMANENT pool of item nodes - built once,
  // never grown or shrunk (this engine has no node-removal path at all;
  // doc/list.md section 1 is the argument for why a fixed pool sidesteps
  // that question rather than answering it). `list_assigned` is a parallel
  // array, one entry per pool slot: which LOGICAL item index that slot
  // currently displays, or -1 for "not yet assigned" (the state right after
  // `attach()`, before the first `list_sync()`). The pool-slot a logical
  // index L is assigned to is always `L % list_pool.size()` (never searched
  // for) - doc/list.md section 2 is the ring-buffer argument in full.
  // `list_item_count`/`list_item_extent` are the FIXED-EXTENT content model
  // this slice declines to generalize (doc/list.md section 3): every item is
  // exactly `list_item_extent` device pixels along `list_axis`, which is
  // what lets the scrollable extent be `list_item_count * list_item_extent`
  // - arithmetic, never a measured child, because there is no real "content"
  // node here to measure.
  ScrollAxis list_axis = ScrollAxis::kNone;
  std::vector<NodeId> list_pool;
  std::vector<int> list_assigned;
  int list_item_count = 0;
  int list_item_extent = 0;

  // kDropdown only. `label` is the plain child showing the currently
  // selected option's text - the same shape kTextField's `content` already
  // is (a paint-time projection the widget positions, never a second node
  // kind); named differently from `content` above only because both fields
  // coexist on the same struct and C++ has one namespace per struct.
  // `options` is the caller-declared choice list, set once via
  // dropdown_set_options() the same way kList's `list_item_count`/
  // `list_item_extent` are set once at attach() time. `selected_index` is
  // RUNTIME STATE for the identical reason `checked`/`value` already are.
  NodeId label;
  std::vector<std::string> options;
  std::optional<int> selected_index;
};

// A normalized [start, end) byte range into a kTextField's Widget::text.
struct TextSelection {
  int start = 0;
  int end = 0;
};

// One pool slot's new identity after `WidgetSet::list_sync()`/
// `list_scroll_by()` reassigned it. `node` never changes once the pool is
// built (doc/list.md section 1); `logical_index` is what the caller must now
// paint there - the whole of the data-source seam this slice needed
// (doc/list.md section 4): the engine reports WHICH node needs WHICH item's
// content, and the caller writes that content through the exact same
// `RenderTree::set_fill`/`set_text`/`set_image` calls an unrecycled node
// would use. No callback, no interface, `virtual`-free by construction
// because there is nothing here for either side to implement against.
struct ListSlot {
  NodeId node;
  int logical_index = 0;
};

// Editing-intent moves a kTextField registers on itself - design.md section
// 5.5.2's "text-editing keys are not shortcuts, they are editing intents the
// TextField itself registers" (line ~595), applied at the smallest scope
// this slice needs rather than through the intent-binding system design.md
// asks for, which does not exist yet (doc/scrolling.md section 1 already
// named this precondition for keyboard scrolling; it is still absent).
enum class TextFieldMove : std::uint8_t {
  kCharLeft,
  kCharRight,
  kLineStart,
  kLineEnd,
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
  //
  // GROUPED CHECKBOXES (Widget::group has a value) behave differently, and
  // that is the whole of what makes one a radio button: rather than
  // flipping, this SELECTS `id` - sets it checked, and clears `checked` on
  // every other kCheckbox sharing the same group - unless `id` is already
  // checked, in which case nothing changes (clicking the already-selected
  // option in a group is a no-op, not a toggle-off; a real radio button is
  // deselected only by another option in its group being selected instead).
  // Only `id`'s own `checked` bit is reported back here - see
  // group_members() for what else this call may have changed, which a
  // caller has to refresh separately because this function has no RenderTree
  // to repaint anything with.
  bool toggle(NodeId id);

  // Every OTHER kCheckbox sharing `id`'s group, in the order attached. Empty
  // when `id` is ungrouped, unattached, or not a checkbox at all.
  //
  // This exists because toggle() cannot repaint what it changes - it has no
  // RenderTree - so a caller that just deselected an entire group by
  // selecting one of its members needs to know WHICH other widgets to
  // refresh(). It is a separate scan rather than something toggle() folds
  // in, matching scrollable_owner_of()'s precedent immediately below: a
  // second, differently-shaped question over the same table, not a
  // multi-purpose answer to both.
  [[nodiscard]] std::vector<NodeId> group_members(NodeId id) const;

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

  // The nearest ancestor-or-self of `id` that is a kList, or nothing - the
  // same shape scrollable_owner_of() has, one control over: a wheel event
  // over a recycled item node must still find the kList that owns it.
  [[nodiscard]] std::optional<NodeId> list_owner_of(const RenderTree& tree, NodeId id) const;

  // Re-derives every kList pool slot's assigned logical index from
  // `top_index` (the item that should sit at the content-origin edge of the
  // viewport) and repositions - via RenderTree::set_local_bounds, never
  // LayoutTree::set_box - any slot whose assignment actually changed.
  // Returns exactly those slots, in no particular order, so the caller
  // refreshes only what changed; an unaffected slot is not returned, which
  // is how a caller doing the minimum necessary work avoids re-deriving
  // "did this change" itself.
  //
  // The one call site both `attach()`-time initial fill (top_index == 0)
  // and a post-layout resync (doc/list.md section 6 - a kLeaf's children
  // are re-placed at its content origin on every relayout, exactly like a
  // slider's thumb, so a caller must call this again after any layout()
  // that touched this node) and list_scroll_by() (below) route through -
  // one recycling primitive, not three.
  std::vector<ListSlot> list_sync(RenderTree& tree, NodeId id, int top_index);

  // Moves a kList's pixel offset by (dx, dy) on `list_axis`, clamped to
  // [0, list_item_count * list_item_extent - viewport_extent], then calls
  // list_sync() with the new top_index. `viewport_extent` is a parameter for
  // the identical reason scroll_by()'s `viewport_content` is: this file
  // depends on RenderTree alone, never LayoutTree.
  //
  // Returns list_sync()'s vector verbatim - empty both when the offset did
  // not move at all (already clamped) AND when it moved by less than one
  // item's extent (an ordinary sub-item wheel notch, which set_scroll_offset
  // alone already paints correctly - doc/list.md section 5).
  std::vector<ListSlot> list_scroll_by(RenderTree& tree, NodeId id, int viewport_extent, int dx,
                                       int dy);

  // The nearest ancestor-or-self of `id` that is a kSlider, or nothing - the
  // same shape scrollable_owner_of() has, one control over: a drag starting
  // on the thumb (a plain, non-interactive child) still has to find the
  // slider that owns it.
  [[nodiscard]] std::optional<NodeId> slidable_owner_of(const RenderTree& tree,
                                                        NodeId id) const;

  // Sets `id`'s value, clamped to [min_value, max_value] and snapped to the
  // nearest `step` when it is positive, then repositions the thumb to match.
  //
  // Returns false, and repositions nothing, when `id` does not name a
  // kSlider or the clamped/snapped value equals what is already stored -
  // scroll_by()'s identical "was this worth a repaint" signal, which is what
  // keeps a drag that has run past the track's end from damaging the thumb
  // on every further pointer-move event.
  //
  // UNLIKE scroll_by(), this takes no external content-box parameter: the
  // track is a plain kLeaf with no padding, so its own local_bounds() already
  // IS the room the thumb may travel in, and no LayoutTree dependency is
  // needed to ask a second time. A padded track would need one, for the
  // identical reason scroll_by() needs `viewport_content` - doc/form-
  // controls.md section 2 names this as declined rather than silently
  // assumed away.
  bool set_slider_value(RenderTree& tree, NodeId id, float value);

  [[nodiscard]] float slider_value(NodeId id) const;

  // The value an ABSOLUTE pointer x-coordinate maps to, against `id`'s
  // CURRENT track geometry - the read-side counterpart of the arithmetic
  // set_slider_value() runs in the other direction. A drag handler uses this
  // to turn a pointer position into a value before calling
  // set_slider_value(); it is exposed rather than folded into a combined
  // "drag to here" entry point so a test can pin the mapping on its own,
  // matching every other piece of geometry in this file.
  [[nodiscard]] float slider_value_at(const RenderTree& tree, NodeId id, int pointer_x) const;

  // Repositions every kSlider's thumb from its CURRENTLY STORED value,
  // against the track and thumb's CURRENT size. Must be called once after
  // building a scene (nothing has ever positioned the thumb yet) and again
  // after any LayoutTree::layout()/layout_full() that may have resized a
  // slider's track - such a pass reassigns the thumb's local origin back to
  // the leaf's ordinary content-origin default, the same way it does for
  // every other leaf child, which silently discards the drag position a
  // plain set_slider_value() call would skip re-deriving once the stored
  // value itself has not changed. This is the slider's counterpart of
  // widget_scene::resync() re-running a stale cached hover after a reflow
  // (doc/widgets.md section 4) - a caller obligation LayoutTree cannot
  // discharge on its own, because it has no notion that this leaf's child
  // position is anything but the ordinary default it just computed.
  void resync_sliders(RenderTree& tree) const;

  // --- kDropdown ---

  [[nodiscard]] const std::vector<std::string>& dropdown_options(NodeId id) const;
  [[nodiscard]] std::optional<int> dropdown_selected_index(NodeId id) const;

  // Sets the option list and refreshes the anchor's own label from
  // whatever `selected_index` already is (out of range after a shrink is
  // treated as unset, not clamped, so a stale index cannot mislabel a
  // shorter list). Construction-time, then read-only, the same "set once,
  // read many" shape kList's `list_item_count`/`list_item_extent` already
  // are - a caller rebuilding the choices rebuilds the widget rather than
  // mutating it live.
  void dropdown_set_options(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                            std::vector<std::string> options);

  // Commits `index` as selected and repaints the anchor's label. A no-op
  // (returns false) when `id` does not name a kDropdown, `index` is out of
  // `options`' range, or it already equals the stored selection - the
  // identical "was this worth a repaint" signal set_slider_value() and
  // scroll_by() already give a caller.
  bool dropdown_select(RenderTree& tree, const FontCatalog& fonts, NodeId id, int index);

  // Writes the appearance `id` should have in `state`, and damages nothing
  // when that appearance is already on screen.
  //
  // The comparison is here rather than inside RenderTree::set_fill because
  // this is where the knowledge is: an interaction loop refreshes a widget
  // whenever its state is touched, and a checkbox toggled while hovered would
  // otherwise damage its box for a colour that did not change. Damage has to
  // be a function of what the user can see, not of how often the loop asks.
  void refresh(RenderTree& tree, NodeId id, PointerState state) const;

  // --- kTextField ---
  //
  // A click resolves to a kTextField the same way it resolves to any other
  // interactive widget - through owner_of()/widget_at(), because kTextField
  // IS accepts_pointer() (unlike kSlider/kScrollView). No separate climb is
  // needed: nothing here has kTextField's children accepting pointer input
  // themselves, so a click anywhere in the field's subtree already resolves
  // to the field.

  [[nodiscard]] const std::string& text_field_text(NodeId id) const;
  [[nodiscard]] int text_field_cursor(NodeId id) const;
  [[nodiscard]] std::optional<TextSelection> text_field_selection(NodeId id) const;

  // Replaces the current selection (if any) or inserts at the cursor.
  // `input` is sanitized before it touches the model (7-2b, doc/text-
  // input.md's cross-reference): malformed UTF-8 (a lone continuation byte,
  // a truncated lead, an overlong encoding, a surrogate) is repaired to
  // U+FFFD one invalid byte at a time via dg::sanitize_utf8() - 7-2's own
  // paragraph_build.cpp substitution, reused rather than a second policy -
  // and ASCII control characters (0x00-0x1F, 0x7F) are dropped, since a
  // literal newline/tab has no meaning in this single-line field
  // (multi-line editing is out of this slice's scope). Everything else is
  // kept, including arbitrary well-formed multi-byte UTF-8 - 4-9's
  // printable-ASCII-only filter is gone. Returns false when nothing changed
  // (an all-filtered/empty input with no selection to delete).
  bool text_field_insert(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                         std::string_view input);

  bool text_field_backspace(RenderTree& tree, const FontCatalog& fonts, NodeId id);
  bool text_field_delete_forward(RenderTree& tree, const FontCatalog& fonts, NodeId id);

  // `extend_selection` is Shift+arrow/Home/End: the anchor is set (if not
  // already) BEFORE the cursor moves, so the selection grows from a fixed
  // point; false clears any selection, matching every desktop toolkit's
  // plain-arrow behaviour.
  bool text_field_move(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                       TextFieldMove move, bool extend_selection);

  // Click-to-position: sets the cursor to the byte offset of whichever
  // grapheme-cluster boundary is nearest `pointer_x` (ABSOLUTE device
  // pixels - the same coordinate a PointerEvent carries) - never a byte
  // offset that would land inside a multi-codepoint cluster (7-2b).
  // `extend_selection` is a shift-click or a drag continuation: the anchor
  // is preserved rather than reset, so dragging from an initial click
  // extends a selection one pointer-move event at a time - the same shape
  // a slider's drag already has (doc/form-controls.md section 1.4), reused
  // here for a text selection rather than a value.
  bool text_field_click(RenderTree& tree, const FontCatalog& fonts, NodeId id, int pointer_x,
                        bool extend_selection);

  // Applies the focused/unfocused display mode (doc/text-input.md section
  // 5): focused shows the full string scrolled so the cursor stays visible
  // and draws a steady (non-blinking - no animation clock exists, matching
  // doc/scrolling.md's declined fling for the identical reason) caret;
  // unfocused shows an ellipsis-truncated prefix when the string overflows
  // the field's width, and hides the caret and any selection highlight.
  // FOCUS ITSELF lives in the separate dg::Focus class, not here - the same
  // separation dg::Interaction's hover/press state already has from
  // WidgetSet, so this takes the answer as a parameter rather than storing
  // a second copy of it. Losing focus also ends any in-progress
  // composition (7-3, doc/ime.md section 6) without committing it - the
  // same rule Escape (text_field_cancel_composition()) and a click
  // (text_field_click()) both apply.
  void text_field_set_focus(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                            bool focused);

  // --- kTextField, IME composition (7-3, doc/ime.md) ---

  // Applies one SDL_EVENT_TEXT_EDITING to `id`'s composition preview.
  // `text` is the IME's not-yet-committed preedit string - sanitized
  // through the exact same dg::sanitize_utf8() + control-byte filter
  // text_field_insert() already uses (a composition event is exactly as
  // untrusted as a keystroke or a paste). An EMPTY `text` ends composition
  // without committing anything (equivalent to
  // text_field_cancel_composition()) - a real commit is always a SEPARATE,
  // later text_field_insert() call, never this one.
  //
  // `start_units`/`length_units` are SDL's own "UTF-8 characters" range
  // within `text` (TextEditingEvent's own comment records the unit
  // question this slice found) - converted to a byte range by walking
  // codepoints, clamped to `text`'s own length so an absurd or negative
  // value from a hostile/buggy source cannot read or write out of bounds.
  // A no-op when `id` does not name a kTextField.
  void text_field_composition_update(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                     std::string_view text, int start_units, int length_units);

  // Ends composition (Escape, or any caller that wants to abandon it)
  // WITHOUT committing anything - the model's `text`/`cursor`/
  // `selection_anchor` are left exactly as they were when composition
  // began, because composition never touched them (this file's own
  // argument, restated: only a real commit, through text_field_insert(),
  // ever does). A no-op when `id` is not currently composing.
  void text_field_cancel_composition(RenderTree& tree, const FontCatalog& fonts, NodeId id);

  [[nodiscard]] bool text_field_is_composing(NodeId id) const;

  // The IME's current not-yet-committed preedit string, or empty when `id`
  // is not composing - exposed for a caller/test that wants to display or
  // assert on it directly rather than only its on-screen projection.
  [[nodiscard]] const std::string& text_field_composition_text(NodeId id) const;

 private:
  // Null when `id` names no widget, including when it is past the end of the
  // table. Every accessor goes through these rather than testing has() and
  // then dereferencing: the two-step form is correct but leaves the check and
  // the access in different expressions, which is a shape neither a reader nor
  // clang-analyzer can follow.
  [[nodiscard]] const Widget* find(NodeId id) const;
  [[nodiscard]] Widget* find(NodeId id);

  // The arithmetic set_slider_value() and resync_sliders() share: move
  // `widget.thumb` to the pixel position its CURRENT `value` maps to. Taking
  // `widget` by reference rather than looking it up a second time is what
  // lets resync_sliders() call this for every kSlider in one pass without
  // re-deriving the NodeId -> Widget* lookup it already has.
  static void reposition_slider(RenderTree& tree, NodeId id, const Widget& widget);

  // Replaces widget.text[lo:hi) with `replacement`, moves the cursor to just
  // past the replacement, clears any selection, then re-derives the
  // display - the one place all of insert/backspace/delete meet, so the
  // projection logic (ellipsis vs scroll, caret and highlight geometry)
  // exists in exactly one function rather than three copies of it.
  static bool text_field_replace_range(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                       Widget& widget, int lo, int hi,
                                       std::string_view replacement);

  // Re-derives everything paint-time about a kTextField from its model
  // (text/cursor/selection_anchor) and `focused`: the content child's
  // displayed string (full+scrolled, or ellipsis-truncated), the caret's
  // position and visibility, and the selection highlight's rectangle.
  static void text_field_refresh_display(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                         Widget& widget, bool focused);

  std::vector<std::optional<Widget>> widgets_;
};

}  // namespace dg
