// The seam between the property table and the two concrete style structs.
//
// Every decision this file makes is recorded in doc/properties.md; what
// follows are only the ones a reader needs in order to follow the code.
//
// THE DISPATCH IS GENERATED. Both switches below are
// src/render/prop_dispatch.generated.inc, included twice with different
// macros. That is what makes the seam drift-proof: a property appended to
// props/drawgui.props.toml grows a case in both switches automatically, and
// the case names `apply_<field>`, so a property added without a decision here
// fails to compile with the missing handler's name in the error. It cannot
// become a silent no-op, which is the failure mode design.md section 5.8
// decision 7 forbids.
//
// NOTHING IS CAST INTO AN ENUMERATION. `prop_id` is a std::uint16_t all the
// way down and the generated `default:` arm is the only thing that decides an
// id is unknown. prop_ids.generated.h records what clang-tidy measured against
// the enumeration form this replaced.
//
// A REJECTED WRITE CHANGES NOTHING. Every applier works on the copies of
// BoxStyle and NodeStyle held in Target and sets a flag; set_prop() commits
// them only after the dispatch has succeeded. Committing as it went would let
// a value rejected halfway leave a node in a state no caller asked for.

#include "drawgui/props/node_props.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"

namespace dg {
namespace {

// The largest magnitude a length may carry, and it is a correctness bound
// rather than a taste one. Layout is integer device pixels and adds bounds
// together (padding to border, child extent to margin), so a value near
// INT_MAX overflows on the first addition - which is undefined, and which
// UBSan would catch only on the runs that happened to reach it. 2^24 is the
// largest integer every float represents exactly, so a value that survives
// this test round-trips through the conversion below without surprise, and it
// is four orders of magnitude past any real viewport.
constexpr float kMaxLength = 16777216.0F;

// Everything an applier may read or write, and nothing else.
//
// The two style structs are COPIES. See the file header: a rejected write must
// not be observable, and the cheapest way to guarantee that is for the applier
// to have nothing but a copy to spoil.
struct Target {
  LayoutTree* tree = nullptr;
  NodeId node;

  BoxStyle box;
  NodeStyle style;
  bool box_changed = false;
  bool style_changed = false;

  // The table gates properties with `applies_to` and `consumed_by`, and both
  // need to know what kind of node this is and what kind holds it.
  //
  // design.md section 5.8 decision 7 defers the parent check to layout time,
  // because there a node may be set up before it is mounted. THIS ENGINE
  // DISAGREES, and can afford to: LayoutTree::add_child takes the parent, so
  // a node cannot exist without one and the answer is already known here. The
  // check is therefore synchronous and the caller learns immediately, which is
  // strictly better than a diagnostic that surfaces one pass later.
  LayoutKind kind = LayoutKind::kLeaf;
  LayoutKind parent_kind = LayoutKind::kLeaf;
  bool is_root = false;
};

[[nodiscard]] bool arranges_children(LayoutKind kind) {
  return kind == LayoutKind::kRow || kind == LayoutKind::kColumn;
}

[[nodiscard]] PropWrite reject(const Target& target, PropStatus status, std::string reason) {
  return PropWrite{status, std::move(reason) + "\n    at " + target.tree->path_of(target.node)};
}

[[nodiscard]] PropWrite unsupported(const Target& target, const std::string& what,
                                    const std::string& needs) {
  return reject(target, PropStatus::kUnsupported,
                "property: " + what + " is in the table but not implemented; " + needs);
}

[[nodiscard]] PropWrite not_applicable(const Target& target, const std::string& what,
                                       const std::string& needs) {
  return reject(target, PropStatus::kNotApplicable,
                "property: " + what + " does not apply to this node; it needs " + needs);
}

[[nodiscard]] PropWrite out_of_range(const Target& target, const std::string& what) {
  return reject(target, PropStatus::kValueOutOfRange, "property: " + what);
}

// A length in the table's float logical pixels, as the integer device pixels
// layout actually speaks.
//
// Rejects rather than saturates, and the difference matters: a caller that
// sent infinity because of its own arithmetic bug wants to hear about it, not
// to receive a very wide box. Non-finite values are the important case -
// static_cast<int>(NaN) is undefined behaviour, so this test is what keeps a
// hostile float from reaching the conversion at all.
//
// Rounds to nearest, ties away from zero. Layout is integral by the deviation
// slice 2 measured its way to (see layout/box.h), so SOME rounding is
// unavoidable here; std::lround is the one that does not favour a direction.
[[nodiscard]] std::optional<int> to_pixels(float value, bool allow_negative) {
  if (!std::isfinite(value)) {
    return std::nullopt;
  }
  if (!allow_negative && value < 0.0F) {
    return std::nullopt;
  }
  if (std::abs(value) > kMaxLength) {
    return std::nullopt;
  }
  return static_cast<int>(std::lround(value));
}

// --------------------------------------------------------------------------
// Shared appliers. Pointer-to-member rather than one function per field: the
// fields differ only in which member they name, and forty hand-written copies
// of the same three lines is how one of them ends up checking the wrong bound.
// --------------------------------------------------------------------------

[[nodiscard]] PropWrite box_scalar(Target& target, const PropValue& value,
                                   int BoxStyle::* member, const std::string& what,
                                   bool allow_negative) {
  const std::optional<int> pixels = to_pixels(value.scalar(), allow_negative);
  if (!pixels.has_value()) {
    return out_of_range(target, what + " needs a finite" +
                                    (allow_negative ? "" : ", non-negative") +
                                    " length within +/-16777216");
  }
  target.box.*member = *pixels;
  target.box_changed = true;
  return PropWrite{};
}

[[nodiscard]] PropWrite box_optional(Target& target, const PropValue& value,
                                     std::optional<int> BoxStyle::* member,
                                     const std::string& what, bool allow_negative) {
  const std::optional<int> pixels = to_pixels(value.scalar(), allow_negative);
  if (!pixels.has_value()) {
    return out_of_range(target, what + " needs a finite" +
                                    (allow_negative ? "" : ", non-negative") +
                                    " length within +/-16777216");
  }
  target.box.*member = pixels;
  target.box_changed = true;
  return PropWrite{};
}

[[nodiscard]] PropWrite box_edge(Target& target, const PropValue& value,
                                 EdgeInsets BoxStyle::* group, int EdgeInsets::* side,
                                 const std::string& what) {
  const std::optional<int> pixels = to_pixels(value.scalar(), false);
  if (!pixels.has_value()) {
    return out_of_range(target,
                        what + " needs a finite, non-negative length within +/-16777216");
  }
  (target.box.*group).*side = *pixels;
  target.box_changed = true;
  return PropWrite{};
}

// A border is the one property in the table that is BOTH layout and paint, and
// the two halves of this engine disagree about its shape.
//
// BoxStyle::border reserves space per side, exactly as the table describes.
// NodeStyle::border_width is a SINGLE uniform stroke, because that is what
// slice 1's painter implements - it insets by half the width and strokes once.
// So four independent painted widths are not expressible today.
//
// The resolution is to paint the largest uniform border that fits inside all
// four reserved insets, which is their minimum. Chosen over the alternatives
// deliberately: rejecting unequal sides would make setting them one at a time
// impossible, since the first write is what makes them unequal; painting the
// maximum would put stroke outside the space some side reserved, and pixels a
// node paints outside the rectangle it declared are exactly what damage
// tracking cannot survive. The minimum is the only choice that is always
// inside the box, and it is exact whenever the four agree - which is every
// case the paint layer can express. doc/properties.md classifies
// border_width_* as partially implemented for this reason.
[[nodiscard]] PropWrite border_edge(Target& target, const PropValue& value,
                                    int EdgeInsets::* side, const std::string& what) {
  PropWrite laid_out = box_edge(target, value, &BoxStyle::border, side, what);
  if (!laid_out.ok()) {
    return laid_out;
  }
  const EdgeInsets& reserved = target.box.border;
  target.style.border_width = static_cast<float>(
      std::min({reserved.left, reserved.top, reserved.right, reserved.bottom}));
  target.style_changed = true;
  return PropWrite{};
}

[[nodiscard]] PropWrite style_color(Target& target, const PropValue& value,
                                    Color NodeStyle::* member) {
  target.style.*member = value.as_color();
  target.style_changed = true;
  return PropWrite{};
}

[[nodiscard]] PropWrite style_radius(Target& target, const PropValue& value,
                                     float Radii::* corner, const std::string& what) {
  const float radius = value.scalar();
  if (!std::isfinite(radius) || radius < 0.0F || radius > kMaxLength) {
    return out_of_range(target,
                        what + " needs a finite, non-negative radius within 0..16777216");
  }
  target.style.radii.*corner = radius;
  target.style_changed = true;
  return PropWrite{};
}

// A parentData property whose consumer this engine can already identify. The
// table says grow is consumed_by flex and left/top/right/bottom by stack; here
// that is "the parent is a row or a column" and "the parent arranges
// absolutely".
[[nodiscard]] std::optional<PropWrite> parent_must_arrange(const Target& target,
                                                           const std::string& what) {
  if (target.is_root) {
    return not_applicable(target, what, "a parent; this is the root");
  }
  if (!arranges_children(target.parent_kind)) {
    return not_applicable(target, what, "a parent that is a row or a column");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<PropWrite> parent_must_be_absolute(const Target& target,
                                                               const std::string& what) {
  if (target.is_root) {
    return not_applicable(target, what, "a parent; this is the root");
  }
  if (target.parent_kind != LayoutKind::kAbsolute) {
    return not_applicable(target, what, "a parent that arranges absolutely");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<PropWrite> self_must_arrange(const Target& target,
                                                         const std::string& what) {
  if (!arranges_children(target.kind)) {
    return not_applicable(target, what, "this node to be a row or a column");
  }
  return std::nullopt;
}

// --------------------------------------------------------------------------
// One applier per property field. The names are load-bearing: the generated
// dispatch spells `apply_<field>` from the TOML's `field` key, so this list
// and the table cannot diverge without the compiler saying so.
// --------------------------------------------------------------------------

PropWrite apply_width(Target& target, const PropValue& value) {
  return box_optional(target, value, &BoxStyle::width, "width", false);
}
PropWrite apply_height(Target& target, const PropValue& value) {
  return box_optional(target, value, &BoxStyle::height, "height", false);
}
PropWrite apply_min_width(Target& target, const PropValue& value) {
  return box_scalar(target, value, &BoxStyle::min_width, "min_width", false);
}
PropWrite apply_max_width(Target& target, const PropValue& value) {
  return box_scalar(target, value, &BoxStyle::max_width, "max_width", false);
}
PropWrite apply_min_height(Target& target, const PropValue& value) {
  return box_scalar(target, value, &BoxStyle::min_height, "min_height", false);
}
PropWrite apply_max_height(Target& target, const PropValue& value) {
  return box_scalar(target, value, &BoxStyle::max_height, "max_height", false);
}

PropWrite apply_aspect_ratio(Target& target, const PropValue& /*value*/) {
  return unsupported(target, "aspect_ratio",
                     "layout would have to derive one axis from the other after the "
                     "constraint resolves, which no arrangement in box_layout.cpp does");
}

PropWrite apply_padding_l(Target& target, const PropValue& value) {
  return box_edge(target, value, &BoxStyle::padding, &EdgeInsets::left, "padding_l");
}
PropWrite apply_padding_t(Target& target, const PropValue& value) {
  return box_edge(target, value, &BoxStyle::padding, &EdgeInsets::top, "padding_t");
}
PropWrite apply_padding_r(Target& target, const PropValue& value) {
  return box_edge(target, value, &BoxStyle::padding, &EdgeInsets::right, "padding_r");
}
PropWrite apply_padding_b(Target& target, const PropValue& value) {
  return box_edge(target, value, &BoxStyle::padding, &EdgeInsets::bottom, "padding_b");
}

// Margin is parentData whose scope is `base`: every container applies it, and
// a leaf places its children too, so there is no parent kind that fails to
// consume it and nothing to gate on.
//
// Negative margins are rejected. CSS allows them; this engine does not model
// them, and BoxConstraints::deflate clamps a shrunk minimum at zero while the
// placement adds the margin verbatim - so a negative margin would move a child
// without giving back the space, asymmetrically. That is a half-implemented
// feature, and doc/properties.md records it as the gap it is.
PropWrite apply_margin_l(Target& target, const PropValue& value) {
  return box_edge(target, value, &BoxStyle::margin, &EdgeInsets::left, "margin_l");
}
PropWrite apply_margin_t(Target& target, const PropValue& value) {
  return box_edge(target, value, &BoxStyle::margin, &EdgeInsets::top, "margin_t");
}
PropWrite apply_margin_r(Target& target, const PropValue& value) {
  return box_edge(target, value, &BoxStyle::margin, &EdgeInsets::right, "margin_r");
}
PropWrite apply_margin_b(Target& target, const PropValue& value) {
  return box_edge(target, value, &BoxStyle::margin, &EdgeInsets::bottom, "margin_b");
}

PropWrite apply_background_color(Target& target, const PropValue& value) {
  return style_color(target, value, &NodeStyle::fill);
}

PropWrite apply_background_gradient(Target& target, const PropValue& /*value*/) {
  return unsupported(target, "background_gradient",
                     "the painter fills one flat colour; a gradient needs an SkShader and "
                     "the dedicated setter design.md section 5.9.5 specifies");
}

PropWrite apply_border_width_l(Target& target, const PropValue& value) {
  return border_edge(target, value, &EdgeInsets::left, "border_width_l");
}
PropWrite apply_border_width_t(Target& target, const PropValue& value) {
  return border_edge(target, value, &EdgeInsets::top, "border_width_t");
}
PropWrite apply_border_width_r(Target& target, const PropValue& value) {
  return border_edge(target, value, &EdgeInsets::right, "border_width_r");
}
PropWrite apply_border_width_b(Target& target, const PropValue& value) {
  return border_edge(target, value, &EdgeInsets::bottom, "border_width_b");
}

PropWrite apply_border_color(Target& target, const PropValue& value) {
  return style_color(target, value, &NodeStyle::border_color);
}

PropWrite apply_border_radius_tl(Target& target, const PropValue& value) {
  return style_radius(target, value, &Radii::top_left, "border_radius_tl");
}
PropWrite apply_border_radius_tr(Target& target, const PropValue& value) {
  return style_radius(target, value, &Radii::top_right, "border_radius_tr");
}
PropWrite apply_border_radius_br(Target& target, const PropValue& value) {
  return style_radius(target, value, &Radii::bottom_right, "border_radius_br");
}
PropWrite apply_border_radius_bl(Target& target, const PropValue& value) {
  return style_radius(target, value, &Radii::bottom_left, "border_radius_bl");
}

PropWrite apply_opacity(Target& target, const PropValue& /*value*/) {
  return unsupported(target, "opacity",
                     "an alpha below 1 has to composite the whole subtree through a "
                     "saveLayer, and no node here has a compositing layer");
}

PropWrite apply_shadow(Target& target, const PropValue& /*value*/) {
  return unsupported(target, "shadow",
                     "an outer shadow paints outside the node's bounds, which damage "
                     "tracking would have to be taught about first");
}

PropWrite apply_overflow(Target& target, const PropValue& /*value*/) {
  return unsupported(target, "overflow",
                     "neither painting nor hit testing clips a child to its parent today, "
                     "and render_tree.h requires that both honour one rule");
}

PropWrite apply_transform(Target& target, const PropValue& /*value*/) {
  return unsupported(target, "transform",
                     "every rectangle here is axis-aligned integer pixels, so a rotated "
                     "node has no damage rectangle to declare");
}

PropWrite apply_direction(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = self_must_arrange(target, "direction");
  if (gate.has_value()) {
    return *gate;
  }
  switch (value.ordinal()) {
    case DG_DIRECTION_ROW:
      target.box.kind = LayoutKind::kRow;
      break;
    case DG_DIRECTION_COLUMN:
      target.box.kind = LayoutKind::kColumn;
      break;
    case DG_DIRECTION_ROW_REVERSE:
    case DG_DIRECTION_COLUMN_REVERSE:
      return unsupported(target, "direction=row_reverse / column_reverse",
                         "size_flex_children walks children in one order and reversing it "
                         "changes which children absorb the integer division remainder");
    default:
      return out_of_range(
          target, "direction has no value with ordinal " + std::to_string(value.ordinal()));
  }
  target.box_changed = true;
  return PropWrite{};
}

PropWrite apply_justify(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = self_must_arrange(target, "justify");
  if (gate.has_value()) {
    return *gate;
  }
  switch (value.ordinal()) {
    case DG_JUSTIFY_START:
      target.box.main_align = MainAlign::kStart;
      break;
    case DG_JUSTIFY_END:
      target.box.main_align = MainAlign::kEnd;
      break;
    case DG_JUSTIFY_CENTER:
      target.box.main_align = MainAlign::kCenter;
      break;
    case DG_JUSTIFY_SPACE_BETWEEN:
      target.box.main_align = MainAlign::kSpaceBetween;
      break;
    case DG_JUSTIFY_SPACE_AROUND:
    case DG_JUSTIFY_SPACE_EVENLY:
      return unsupported(target, "justify=space_around / space_evenly",
                         "MainAlign has four enumerators; both of these also distribute "
                         "space before the first child, which place_flex_children does not");
    default:
      return out_of_range(
          target, "justify has no value with ordinal " + std::to_string(value.ordinal()));
  }
  target.box_changed = true;
  return PropWrite{};
}

PropWrite apply_align(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = self_must_arrange(target, "align");
  if (gate.has_value()) {
    return *gate;
  }
  switch (value.ordinal()) {
    case DG_ALIGN_START:
      target.box.cross_align = CrossAlign::kStart;
      break;
    case DG_ALIGN_END:
      target.box.cross_align = CrossAlign::kEnd;
      break;
    case DG_ALIGN_CENTER:
      target.box.cross_align = CrossAlign::kCenter;
      break;
    case DG_ALIGN_STRETCH:
      target.box.cross_align = CrossAlign::kStretch;
      break;
    case DG_ALIGN_BASELINE:
      return unsupported(target, "align=baseline",
                         "aligning on a baseline needs a child's baseline before it is "
                         "placed, which is intrinsic sizing - deliberately absent, see "
                         "layout_tree.h");
    default:
      return out_of_range(target,
                          "align has no value with ordinal " + std::to_string(value.ordinal()));
  }
  target.box_changed = true;
  return PropWrite{};
}

PropWrite apply_gap(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = self_must_arrange(target, "gap");
  if (gate.has_value()) {
    return *gate;
  }
  return box_scalar(target, value, &BoxStyle::gap, "gap", false);
}

PropWrite apply_main_size(Target& target, const PropValue& /*value*/) {
  return unsupported(target, "main_size",
                     "a container here always shrinks to its content within its limits; "
                     "filling the main axis instead is a second sizing rule");
}

PropWrite apply_run_gap(Target& target, const PropValue& /*value*/) {
  return unsupported(target, "run_gap",
                     "there is no wrapping arrangement, so there are no runs to space");
}

PropWrite apply_align_content(Target& target, const PropValue& /*value*/) {
  return unsupported(target, "align_content",
                     "there is no wrapping arrangement, so there is no run stack to align");
}

// An INTEGER weight, and a fractional one is rejected rather than rounded.
//
// box.h records why the engine's weight is an int: the distribution is exact
// integer division with the remainder handed to the earliest children, so a
// re-run produces the same pixels rather than the same pixels up to rounding,
// and byte-identity between an incremental and a full layout is the acceptance
// bar. Rounding 0.5 to 0 here would silently delete a child's flexibility;
// rounding it to 1 would silently double it against a sibling weighted 1.
PropWrite apply_grow(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = parent_must_arrange(target, "grow");
  if (gate.has_value()) {
    return *gate;
  }
  const float weight = value.scalar();
  if (!std::isfinite(weight) || weight < 0.0F || weight > kMaxLength ||
      weight != std::floor(weight)) {
    return out_of_range(target,
                        "grow needs a whole, non-negative weight; the free-space split is "
                        "exact integer division, so a fractional weight has no meaning "
                        "that survives a re-layout");
  }
  target.box.grow = static_cast<int>(weight);
  target.box_changed = true;
  return PropWrite{};
}

PropWrite apply_shrink(Target& target, const PropValue& /*value*/) {
  return unsupported(target, "shrink",
                     "children that overrun the main axis are reported as a diagnostic and "
                     "left overrunning; absorbing negative free space is a second pass");
}

PropWrite apply_basis(Target& target, const PropValue& /*value*/) {
  return unsupported(target, "basis",
                     "a flexible child is measured under the share its weight earns, with "
                     "no separate base size to start from");
}

PropWrite apply_align_self(Target& target, const PropValue& /*value*/) {
  return unsupported(target, "align_self",
                     "place_flex_children reads the container's cross_align for every "
                     "child; there is no per-child override to read");
}

PropWrite apply_left(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = parent_must_be_absolute(target, "left");
  if (gate.has_value()) {
    return *gate;
  }
  return box_optional(target, value, &BoxStyle::left, "left", true);
}
PropWrite apply_top(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = parent_must_be_absolute(target, "top");
  if (gate.has_value()) {
    return *gate;
  }
  return box_optional(target, value, &BoxStyle::top, "top", true);
}
PropWrite apply_right(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = parent_must_be_absolute(target, "right");
  if (gate.has_value()) {
    return *gate;
  }
  return box_optional(target, value, &BoxStyle::right, "right", true);
}
PropWrite apply_bottom(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = parent_must_be_absolute(target, "bottom");
  if (gate.has_value()) {
    return *gate;
  }
  return box_optional(target, value, &BoxStyle::bottom, "bottom", true);
}

// --------------------------------------------------------------------------
// The generated dispatch.
// --------------------------------------------------------------------------

using Applier = PropWrite (*)(Target&, const PropValue&);

// The type check lives here rather than inside each applier, and it is the
// generated `type` token that supplies the expected type - so a property whose
// TOML type changes starts being checked against the new one without anybody
// editing this file, and no applier can forget the check entirely.
[[nodiscard]] PropWrite checked(Target& target, const PropValue& value, PropType declared,
                                Applier applier) {
  if (value.type() != declared) {
    return reject(target, PropStatus::kTypeMismatch,
                  "property: value has the wrong type for this id");
  }
  return applier(target, value);
}

[[nodiscard]] PropWrite complex_value(Target& target, Applier applier, const PropValue& value) {
  // gradient, shadow and transform do not fit the scalar tagged union and take
  // dedicated setters (design.md section 5.9.5). None of the three is
  // implemented, so routing them to their applier reports exactly that; when
  // one lands, the dedicated setter is what calls it and this arm keeps
  // telling a scalar caller it used the wrong entry point.
  return applier(target, value);
}

[[nodiscard]] PropWrite dispatch(Target& target, dg_prop_id prop_id, const PropValue& value) {
  PropWrite out;
#define DG_PROP_ASSIGN(field, type) \
  out = checked(target, value, PropType::k_##type, &apply_##field)
#define DG_PROP_ASSIGN_COMPLEX(field, type) out = complex_value(target, &apply_##field, value)
#define DG_PROP_UNKNOWN                                                                      \
  out = reject(target, PropStatus::kUnknownId,                                               \
               "property: no property has id " + std::to_string(prop_id) + "; ids run 1.." + \
                   std::to_string(kDgPropMaxId))
#include "render/prop_dispatch.generated.inc"
#undef DG_PROP_ASSIGN
#undef DG_PROP_ASSIGN_COMPLEX
#undef DG_PROP_UNKNOWN
  return out;
}

}  // namespace

std::optional<PropType> prop_type(dg_prop_id prop_id) {
  std::optional<PropType> found;
#define DG_PROP_ASSIGN(field, type) found = PropType::k_##type
#define DG_PROP_ASSIGN_COMPLEX(field, type) found = PropType::k_##type
#define DG_PROP_UNKNOWN found = std::nullopt
#include "render/prop_dispatch.generated.inc"
#undef DG_PROP_ASSIGN
#undef DG_PROP_ASSIGN_COMPLEX
#undef DG_PROP_UNKNOWN
  return found;
}

PropWrite set_prop(LayoutTree& tree, NodeId node, dg_prop_id prop_id, const PropValue& value) {
  const NodeId parent = tree.render().parent(node);

  Target target;
  target.tree = &tree;
  target.node = node;
  target.box = tree.box(node);
  target.style = tree.render().style(node);
  target.kind = target.box.kind;
  target.parent_kind = tree.box(parent).kind;
  target.is_root = parent == node;

  PropWrite out = dispatch(target, prop_id, value);
  if (!out.ok()) {
    return out;
  }

  // This early return is what makes "a rejected write changes nothing" true
  // for an applier that validates LATE, and it was worth proving rather than
  // asserting. Injecting a late-validating applier on its own - one that
  // mutates its copy and then returns a failure - leaves every test green,
  // because this line absorbs it. Injecting it TOGETHER with a commit-on-
  // failure here turns four boundary cases red. So the guard is load-bearing
  // under a change nobody has made yet, which is exactly when a guard is worth
  // keeping.
  //
  // Appliers should still validate before they mutate; that is the cheaper of
  // the two defences and the one that keeps this file readable.
  //
  // Only what changed is committed. Writing both unconditionally would
  // relayout for a colour change, collapsing the two invalidation classes
  // design.md section 5.15.2 separates by cost.
  if (target.box_changed) {
    tree.set_box(node, target.box);
  }
  if (target.style_changed) {
    tree.render().set_style(node, target.style);
  }
  return out;
}

}  // namespace dg
