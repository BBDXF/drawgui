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
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

[[nodiscard]] bool distributes_free_space(LayoutKind kind) {
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

// A border is the one property in the table that is BOTH layout and paint.
//
// BoxStyle::border reserves space per side and NodeStyle::border_width now
// paints per side, so the two agree exactly and the table-versus-engine
// disagreement doc/properties.md section 3.3 recorded is gone. Until this
// slice the painter carried a single uniform stroke and this function wrote
// the MINIMUM of the four - the only choice that was always inside the box,
// and exact only when the four agreed.
[[nodiscard]] PropWrite border_edge(Target& target, const PropValue& value,
                                    int EdgeInsets::* side, const std::string& what) {
  PropWrite laid_out = box_edge(target, value, &BoxStyle::border, side, what);
  if (!laid_out.ok()) {
    return laid_out;
  }
  const EdgeInsets& reserved = target.box.border;
  target.style.border_width =
      BorderWidths{static_cast<float>(reserved.left), static_cast<float>(reserved.top),
                   static_cast<float>(reserved.right), static_cast<float>(reserved.bottom)};
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
//
// `grow` stays flex-only even though a wrapping container arranges children
// too, and that agrees with both halves of the record: the table lists
// consumed_by = ["flex"], and design.md section 5.4.4 excludes grow from
// RenderWrap outright. align_self is the one that widened - it reads
// consumed_by = ["flex", "wrap"], because a run aligns on the cross axis
// exactly as a single-line flex does.
[[nodiscard]] std::optional<PropWrite> parent_must_flex(const Target& target,
                                                        const std::string& what) {
  if (target.is_root) {
    return not_applicable(target, what, "a parent; this is the root");
  }
  if (!distributes_free_space(target.parent_kind)) {
    return not_applicable(target, what, "a parent that is a row or a column");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<PropWrite> parent_must_arrange(const Target& target,
                                                           const std::string& what) {
  if (target.is_root) {
    return not_applicable(target, what, "a parent; this is the root");
  }
  if (!arranges_children(target.parent_kind)) {
    return not_applicable(target, what, "a parent that arranges children in a line");
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
    return not_applicable(target, what, "this node to be a row, a column or a wrap");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<PropWrite> self_must_wrap(const Target& target,
                                                      const std::string& what) {
  if (!wraps_children(target.kind)) {
    return not_applicable(target, what, "this node to be a wrapping container");
  }
  return std::nullopt;
}

// `scroll_axis` changes the constraint a kLeaf hands its single child
// (measure_leaf), which is meaningless on a node that arranges its OWN
// children in a row, column or run - those already decide their children's
// constraints a different way, and none of them has "one child" as a
// precondition the way a leaf does.
[[nodiscard]] std::optional<PropWrite> self_must_be_leaf(const Target& target,
                                                         const std::string& what) {
  if (target.kind != LayoutKind::kLeaf) {
    return not_applicable(target, what,
                          "this node to be a plain box (a leaf), not a row/column/wrap");
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

// A RATIO, so neither a length nor a fraction: zero and negative values have
// no geometry, and a value near either end of the float range turns a modest
// settled axis into an extent layout cannot add to. Bounded symmetrically so
// that `w/h` and `h/w` are equally expressible - 1/16777216 through 16777216.
PropWrite apply_aspect_ratio(Target& target, const PropValue& value) {
  const float ratio = value.scalar();
  if (!std::isfinite(ratio) || ratio <= 0.0F || ratio > kMaxLength ||
      ratio < 1.0F / kMaxLength) {
    return out_of_range(target,
                        "aspect_ratio needs a finite, positive width/height ratio within "
                        "1/16777216..16777216");
  }
  target.box.aspect_ratio = ratio;
  target.box_changed = true;
  return PropWrite{};
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
  return reject(target, PropStatus::kTypeMismatch,
                "property: background_gradient IS implemented, through the dedicated "
                "dg::set_gradient() channel (design.md section 5.9.5) rather than through "
                "this scalar entry point - a multi-stop gradient descriptor does not fit "
                "PropValue's tagged union");
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

// The one property in the table whose unit is neither a length nor a colour:
// a plain 0..1 fraction, and out of that range it is refused rather than
// clamped. Clamping would let a caller whose own arithmetic produced 1.4 or
// -0.2 - the overshoot of an easing curve is the obvious source - never find
// out, and the two ends fail differently enough to be worth telling apart: a
// value above 1 means the animation is running past its endpoint, a value
// below 0 means it is running past its start.
PropWrite apply_opacity(Target& target, const PropValue& value) {
  const float opacity = value.scalar();
  if (!std::isfinite(opacity) || opacity < 0.0F || opacity > 1.0F) {
    return out_of_range(target, "opacity needs a fraction in 0..1");
  }
  target.style.opacity = opacity;
  target.style_changed = true;
  return PropWrite{};
}

PropWrite apply_shadow(Target& target, const PropValue& /*value*/) {
  return reject(target, PropStatus::kTypeMismatch,
                "property: shadow IS implemented, through the dedicated dg::set_shadow() "
                "channel (design.md section 5.9.5) rather than through this scalar entry "
                "point - an offset/blur/spread/colour descriptor does not fit PropValue's "
                "tagged union");
}

PropWrite apply_overflow(Target& target, const PropValue& value) {
  switch (value.ordinal()) {
    case DG_OVERFLOW_VISIBLE:
      target.style.overflow = Overflow::kVisible;
      break;
    case DG_OVERFLOW_CLIP:
      target.style.overflow = Overflow::kClip;
      break;
    default:
      return out_of_range(
          target, "overflow has no value with ordinal " + std::to_string(value.ordinal()));
  }
  target.style_changed = true;
  return PropWrite{};
}

PropWrite apply_transform(Target& target, const PropValue& /*value*/) {
  return unsupported(target, "transform",
                     "every rectangle here is axis-aligned integer pixels, so a rotated "
                     "node has no damage rectangle to declare; dg::set_transform() (the "
                     "dedicated setter's fourth client, added this slice) reports the "
                     "identical status through the id-based channel");
}

// The value names a decoded ImageCatalog entry, which does not fit the
// scalar tagged union any more than a gradient or a shadow descriptor does -
// design.md section 5.9.5's dg_node_set_image is the dedicated setter this
// still needs. Unlike gradient/shadow/transform, this one already has a real
// consumer (RenderTree::set_image(), src/render/render_tree.cpp) for a
// caller reaching it directly in C++; what is missing is only the id-based
// channel a host language would use, which slice 5-4 is scheduled to build
// for gradient/shadow/transform and could equally make image its fourth
// client - doc/image.md section on the property table records the decision
// not to build that channel in this slice.
PropWrite apply_image_source(Target& target, const PropValue& /*value*/) {
  return reject(target, PropStatus::kTypeMismatch,
                "property: image_source IS implemented, through the dedicated "
                "dg::set_image() channel (design.md section 5.9.5) or "
                "RenderTree::set_image() directly, rather than through this scalar entry "
                "point - the value names a decoded ImageCatalog entry, which does not fit "
                "PropValue's tagged union");
}

PropWrite apply_image_fit(Target& target, const PropValue& value) {
  switch (value.ordinal()) {
    case DG_IMAGE_FIT_FILL:
      target.style.image.fit = ImageFit::kFill;
      break;
    case DG_IMAGE_FIT_CONTAIN:
      target.style.image.fit = ImageFit::kContain;
      break;
    case DG_IMAGE_FIT_COVER:
      target.style.image.fit = ImageFit::kCover;
      break;
    case DG_IMAGE_FIT_NONE:
      target.style.image.fit = ImageFit::kNone;
      break;
    default:
      return out_of_range(
          target, "image_fit has no value with ordinal " + std::to_string(value.ordinal()));
  }
  target.style_changed = true;
  return PropWrite{};
}

PropWrite apply_image_placeholder_color(Target& target, const PropValue& value) {
  target.style.image.placeholder = value.as_color();
  target.style_changed = true;
  return PropWrite{};
}

PropWrite apply_direction(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = self_must_arrange(target, "direction");
  if (gate.has_value()) {
    return *gate;
  }
  switch (value.ordinal()) {
    case DG_DIRECTION_ROW:
      target.box.kind = wraps_children(target.kind) ? LayoutKind::kWrapRow : LayoutKind::kRow;
      break;
    case DG_DIRECTION_COLUMN:
      target.box.kind =
          wraps_children(target.kind) ? LayoutKind::kWrapColumn : LayoutKind::kColumn;
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

PropWrite apply_main_size(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = self_must_arrange(target, "main_size");
  if (gate.has_value()) {
    return *gate;
  }
  switch (value.ordinal()) {
    case DG_MAIN_SIZE_MIN:
      target.box.main_size = MainSize::kMin;
      break;
    case DG_MAIN_SIZE_MAX:
      target.box.main_size = MainSize::kMax;
      break;
    default:
      return out_of_range(
          target, "main_size has no value with ordinal " + std::to_string(value.ordinal()));
  }
  target.box_changed = true;
  return PropWrite{};
}

PropWrite apply_run_gap(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = self_must_wrap(target, "run_gap");
  if (gate.has_value()) {
    return *gate;
  }
  return box_scalar(target, value, &BoxStyle::run_gap, "run_gap", false);
}

PropWrite apply_align_content(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = self_must_wrap(target, "align_content");
  if (gate.has_value()) {
    return *gate;
  }
  switch (value.ordinal()) {
    case DG_ALIGN_CONTENT_START:
      target.box.align_content = AlignContent::kStart;
      break;
    case DG_ALIGN_CONTENT_END:
      target.box.align_content = AlignContent::kEnd;
      break;
    case DG_ALIGN_CONTENT_CENTER:
      target.box.align_content = AlignContent::kCenter;
      break;
    case DG_ALIGN_CONTENT_STRETCH:
      target.box.align_content = AlignContent::kStretch;
      break;
    case DG_ALIGN_CONTENT_SPACE_BETWEEN:
      target.box.align_content = AlignContent::kSpaceBetween;
      break;
    case DG_ALIGN_CONTENT_SPACE_AROUND:
      target.box.align_content = AlignContent::kSpaceAround;
      break;
    default:
      return out_of_range(
          target, "align_content has no value with ordinal " + std::to_string(value.ordinal()));
  }
  target.box_changed = true;
  return PropWrite{};
}

PropWrite apply_scroll_axis(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = self_must_be_leaf(target, "scroll_axis");
  if (gate.has_value()) {
    return *gate;
  }
  switch (value.ordinal()) {
    case DG_SCROLL_AXIS_NONE:
      target.box.scroll_axis = ScrollAxis::kNone;
      break;
    case DG_SCROLL_AXIS_VERTICAL:
      target.box.scroll_axis = ScrollAxis::kVertical;
      break;
    case DG_SCROLL_AXIS_HORIZONTAL:
      target.box.scroll_axis = ScrollAxis::kHorizontal;
      break;
    default:
      return out_of_range(
          target, "scroll_axis has no value with ordinal " + std::to_string(value.ordinal()));
  }
  target.box_changed = true;
  return PropWrite{};
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
  const std::optional<PropWrite> gate = parent_must_flex(target, "grow");
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

// A whole weight, refused rather than rounded, for the reason grow is: the
// deficit split is exact integer division and a fractional weight has no
// meaning that survives a re-layout.
//
// ACCEPTED EVEN WHEN IT CANNOT BE HONOURED, deliberately. shrink needs a
// declared base - a `basis`, or a definite size on the container's main axis -
// and whether the child has one depends on properties that may be written in
// either order, so refusing here would make `shrink` then `basis` fail where
// `basis` then `shrink` succeeded. The refusal belongs at layout time, where
// the whole node is visible, and size_flex_children() reports it with the node
// path. Same shape as align_self=stretch under a wrapping parent.
PropWrite apply_shrink(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = parent_must_flex(target, "shrink");
  if (gate.has_value()) {
    return *gate;
  }
  const float weight = value.scalar();
  if (!std::isfinite(weight) || weight < 0.0F || weight > kMaxLength ||
      weight != std::floor(weight)) {
    return out_of_range(target,
                        "shrink needs a whole, non-negative weight; the deficit split is "
                        "exact integer division, so a fractional weight has no meaning "
                        "that survives a re-layout");
  }
  target.box.shrink = static_cast<int>(weight);
  target.box_changed = true;
  return PropWrite{};
}

// Like `width`, this has no way to be cleared once written, because the table
// carries no `auto` ordinal for it and inventing one here would be inventing
// ABI. `align_self` can be cleared only because its `values` list has `auto` in
// it. Recorded rather than worked around.
PropWrite apply_basis(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = parent_must_flex(target, "basis");
  if (gate.has_value()) {
    return *gate;
  }
  return box_optional(target, value, &BoxStyle::basis, "basis", false);
}

// `auto` is absence rather than a fifth alignment, which is why BoxStyle holds
// an optional. Writing `auto` CLEARS the override rather than recording one,
// so a node can be handed back to its container after being taken off it.
//
// `stretch` is accepted here and may still not be honoured: under a WRAPPING
// container it degrades to start, and the arrangement says so at layout time
// with the node path attached. That is not this gate's call to make, because
// the answer depends on the container, and a child's align_self outlives any
// particular parent kind - a node whose container later becomes a flex must
// not have had its stretch silently discarded at set time.
PropWrite apply_align_self(Target& target, const PropValue& value) {
  const std::optional<PropWrite> gate = parent_must_arrange(target, "align_self");
  if (gate.has_value()) {
    return *gate;
  }
  switch (value.ordinal()) {
    case DG_ALIGN_SELF_AUTO:
      target.box.align_self.reset();
      break;
    case DG_ALIGN_SELF_START:
      target.box.align_self = CrossAlign::kStart;
      break;
    case DG_ALIGN_SELF_END:
      target.box.align_self = CrossAlign::kEnd;
      break;
    case DG_ALIGN_SELF_CENTER:
      target.box.align_self = CrossAlign::kCenter;
      break;
    case DG_ALIGN_SELF_STRETCH:
      target.box.align_self = CrossAlign::kStretch;
      break;
    case DG_ALIGN_SELF_BASELINE:
      return unsupported(target, "align_self=baseline",
                         "aligning on a baseline needs a child's baseline before it is "
                         "placed, which is intrinsic sizing - deliberately absent, see "
                         "layout_tree.h");
    default:
      return out_of_range(
          target, "align_self has no value with ordinal " + std::to_string(value.ordinal()));
  }
  target.box_changed = true;
  return PropWrite{};
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

// --------------------------------------------------------------------------
// The dedicated-setter channel (design.md section 5.9.5). node_props.h has
// the full design; this is the shared prelude every one of the four
// functions there opens with, extracted from set_image() once it worked.
// --------------------------------------------------------------------------

namespace {

// Validates `prop_id` against the type the CALLER's C++ signature already
// commits to - the complex-channel equivalent of checked()'s scalar type
// check above, and deliberately built the same shape: kUnknownId when the id
// names nothing, kTypeMismatch when it names a property of a DIFFERENT
// complex type (dg::set_shadow() called with DG_PROP_BACKGROUND_GRADIENT,
// say). Returns nullopt to mean "go ahead".
[[nodiscard]] std::optional<PropWrite> complex_prop_prelude(const LayoutTree& tree, NodeId node,
                                                            dg_prop_id prop_id,
                                                            PropType expected) {
  const std::optional<PropType> declared = prop_type(prop_id);
  if (!declared.has_value()) {
    return PropWrite{PropStatus::kUnknownId, "property: no property has id " +
                                                 std::to_string(prop_id) + "; ids run 1.." +
                                                 std::to_string(kDgPropMaxId) + "\n    at " +
                                                 tree.path_of(node)};
  }
  if (*declared != expected) {
    return PropWrite{PropStatus::kTypeMismatch,
                     "property: id " + std::to_string(prop_id) +
                         " does not name a property of this dedicated setter's complex "
                         "type\n    at " +
                         tree.path_of(node)};
  }
  return std::nullopt;
}

[[nodiscard]] PropWrite complex_out_of_range(const LayoutTree& tree, NodeId node,
                                             const std::string& reason) {
  return PropWrite{PropStatus::kValueOutOfRange,
                   "property: " + reason + "\n    at " + tree.path_of(node)};
}

// Skia's own `SkGradient::Colors` contract (include/effects/SkGradient.h):
// positions must be finite, lie in 0..1 and be STRICTLY increasing. 32 is not
// a measured limit, only a sanity bound against an unbounded allocation from
// an untrusted caller - nothing in this slice's example needs more than four.
constexpr std::size_t kMaxGradientStops = 32;

[[nodiscard]] bool valid_gradient_stops(const std::vector<GradientStop>& stops) {
  if (stops.size() < 2 || stops.size() > kMaxGradientStops) {
    return false;
  }
  float previous = -1.0F;
  for (const GradientStop& stop : stops) {
    if (!std::isfinite(stop.offset) || stop.offset < 0.0F || stop.offset > 1.0F) {
      return false;
    }
    if (stop.offset <= previous) {
      return false;
    }
    previous = stop.offset;
  }
  return true;
}

// The shadow budget. doc/cpu-raster-findings.md measured blur (including drop
// shadow) at 52% of a dense scene's raster time - the single most expensive
// thing this engine can be asked to paint - so blur_radius is capped rather
// than left open the way an ordinary length is. The other three bounds are
// sanity limits against a hostile or buggy caller, not measurements: nothing
// about this engine's damage system breaks above them, they simply stop
// answers that could not be a real design intent (a shadow offset wider than
// a 4K display, say).
constexpr float kMaxShadowBlur = 48.0F;
constexpr float kMaxShadowSpread = 64.0F;
constexpr float kMaxShadowOffset = 512.0F;

[[nodiscard]] bool valid_shadow(const ShadowStyle& shadow) {
  return std::isfinite(shadow.offset_x) && std::isfinite(shadow.offset_y) &&
         std::isfinite(shadow.blur_radius) && std::isfinite(shadow.spread) &&
         shadow.blur_radius >= 0.0F && shadow.blur_radius <= kMaxShadowBlur &&
         std::abs(shadow.spread) <= kMaxShadowSpread &&
         std::abs(shadow.offset_x) <= kMaxShadowOffset &&
         std::abs(shadow.offset_y) <= kMaxShadowOffset;
}

}  // namespace

// THE PROTOTYPE. Built and proven before any of the other three, then
// complex_prop_prelude() above was extracted from what worked here - node_
// props.h records why image_source, specifically, was the right thing to
// build first rather than last.
PropWrite set_image(LayoutTree& tree, NodeId node, dg_prop_id prop_id,
                    const ImageStyle& image) {
  const std::optional<PropWrite> rejected =
      complex_prop_prelude(tree, node, prop_id, PropType::k_image);
  if (rejected.has_value()) {
    return *rejected;
  }
  // RenderTree::set_image() already damages correctly and already dedupes an
  // unchanged value (slice 5-1) - this door adds only the id check above, not
  // a second copy of what that function already does.
  tree.render().set_image(node, image);
  return PropWrite{};
}

PropWrite set_gradient(LayoutTree& tree, NodeId node, dg_prop_id prop_id,
                       const LinearGradientStyle& gradient) {
  const std::optional<PropWrite> rejected =
      complex_prop_prelude(tree, node, prop_id, PropType::k_gradient);
  if (rejected.has_value()) {
    return *rejected;
  }
  if (!std::isfinite(gradient.angle_deg)) {
    return complex_out_of_range(tree, node, "background_gradient needs a finite angle_deg");
  }
  if (!valid_gradient_stops(gradient.stops)) {
    return complex_out_of_range(tree, node,
                                "background_gradient needs 2.." +
                                    std::to_string(kMaxGradientStops) +
                                    " stops with finite, strictly increasing offsets in 0..1");
  }
  NodeStyle style = tree.render().style(node);
  style.background_gradient = gradient;
  tree.render().set_style(node, style);
  return PropWrite{};
}

PropWrite set_shadow(LayoutTree& tree, NodeId node, dg_prop_id prop_id,
                     const ShadowStyle& shadow) {
  const std::optional<PropWrite> rejected =
      complex_prop_prelude(tree, node, prop_id, PropType::k_shadow);
  if (rejected.has_value()) {
    return *rejected;
  }
  if (!valid_shadow(shadow)) {
    return complex_out_of_range(
        tree, node,
        "shadow needs a finite offset/spread within +/-" +
            std::to_string(static_cast<int>(kMaxShadowOffset)) +
            " and a blur_radius in "
            "0.." +
            std::to_string(static_cast<int>(kMaxShadowBlur)) +
            " - budgeted per doc/cpu-raster-findings.md's measured blur cost");
  }
  NodeStyle style = tree.render().style(node);
  style.shadow = shadow;
  tree.render().set_style(node, style);
  return PropWrite{};
}

// The fourth client, and the one that always refuses. node_props.h has the
// full argument; this is only the mechanical half of it.
PropWrite set_transform(LayoutTree& tree, NodeId node, dg_prop_id prop_id,
                        const TransformDesc& /*transform*/) {
  const std::optional<PropWrite> rejected =
      complex_prop_prelude(tree, node, prop_id, PropType::k_transform);
  if (rejected.has_value()) {
    return *rejected;
  }
  return PropWrite{PropStatus::kUnsupported,
                   "property: transform is not implemented - every rectangle this engine "
                   "tracks is axis-aligned integer pixels (damage, hit testing and "
                   "clipping all speak PixelRect), so a general 2D transform needs "
                   "non-axis-aligned damage bounds, an inverse-transform hit test and a "
                   "decision about whether it affects parent layout, none of which exists "
                   "yet\n    at " +
                       tree.path_of(node)};
}

}  // namespace dg
