// Setting a node's layout and paint from the generated property table.
//
// This is the consumer props/drawgui.props.toml was written for and then
// waited four slices without. The table, the generator and the ABI lock were
// built in P0 and frozen because nothing included them; slices 2 and 3 grew
// their own concrete vocabulary - BoxStyle, NodeStyle - because at the time
// nothing justified reaching for a table nobody read. Two vocabularies for the
// same concepts is the drift the whole generator exists to prevent, so this
// header exists to say which one is which.
//
// THE RECONCILIATION, in one sentence: the TABLE is the external vocabulary
// and BoxStyle/NodeStyle are the internal storage, and set_prop() below is the
// single seam between them.
//
// Neither replaces the other, and that is not a fudge:
//
//   The table cannot be the storage. It describes float logical pixels with
//   percentages; slice 2 measured its way to INTEGER DEVICE PIXELS because a
//   damage rectangle is only correct when the rectangle a node declares is
//   exactly the rectangle it paints. Generating BoxStyle from the table would
//   undo that with no measurement behind it.
//
//   The structs cannot be the vocabulary. A host language calling the eventual
//   C ABI sets a property by NUMBER; it cannot name a C++ struct field. The
//   numbers are an ABI contract the lock already guards.
//
// What removes the ambiguity is that the seam is GENERATED. set_prop() is
// implemented by including src/render/prop_dispatch.generated.inc, so every
// property in the TOML gets a case whether or not anybody remembered it, and
// a property added to the table without a decision here is a COMPILE ERROR
// naming the missing handler rather than a silent no-op. doc/properties.md
// records the decision, the disagreements the reconciliation exposed, and the
// per-property gap report.
//
// NOT EVERY PROPERTY IS IMPLEMENTED, on purpose. Several describe capability
// the engine does not have - aspect_ratio, shadow, transform, overflow, flex
// wrapping. Those report kUnsupported, observably, one line of code each, and
// doc/properties.md says what each of them needs. That list is the input to
// the next slice.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "drawgui/graphics/types.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"

namespace dg {

class LayoutTree;

// What kind of value a property expects.
//
// THE ENUMERATOR NAMES ARE BOUND TO THE `type` TOKENS in
// props/drawgui.props.toml. prop_dispatch.generated.inc pastes those tokens
// straight into `PropType::k_##type`, so a new type in the TOML is a compile
// error here rather than a silent fall-through onto an existing one. That is
// the reason for the un-house-styled spelling, and it is worth the ugliness:
// the alternative is a hand-written token-to-enumerator map, which is exactly
// the second copy this file exists to avoid.
enum class PropType : std::uint8_t {
  k_float,
  k_length,
  k_color,
  k_enum,
  k_gradient,
  k_shadow,
  k_transform,
  k_image,
};

// The declared type of `prop_id`, or nothing when no property has that id.
//
// Generated-backed, so it answers for all 45 rather than for the ones this
// slice implements - a caller type-checking against the table is asking about
// the ABI, not about today's coverage.
[[nodiscard]] std::optional<PropType> prop_type(dg_prop_id prop_id);

// How a write ended. Every outcome is reported; none is silent.
//
// design.md section 5.8 decision 7 forbids silently ignoring a write, and the
// reason generalises past parentData: a host language that cannot tell the
// difference between "applied" and "dropped" has no way to find its own bug.
enum class PropStatus : std::uint8_t {
  kApplied,

  // No property has this id. Includes 0 (DG_PROP_INVALID) and every value
  // above kDgPropMaxId, which is most of the uint16_t range.
  kUnknownId,

  // The id names a real property, but the value carries a different type than
  // the table declares for it - a colour sent to `width`, say.
  kTypeMismatch,

  // Right type, unusable value: a non-finite float, a magnitude layout cannot
  // represent, a negative length, or an enum ordinal past the property's
  // `values` list.
  kValueOutOfRange,

  // The property is real and the value is fine, but it does not apply to THIS
  // node - `gap` on a leaf, `grow` on a child whose parent is not a row or a
  // column. The table's `applies_to` / `consumed_by` fields are what decide.
  kNotApplicable,

  // The engine does not implement this property yet. Distinct from every
  // status above because it is a statement about drawgui, not about the
  // caller: the same write becomes kApplied when a later slice lands the
  // capability. doc/properties.md lists every one and what it needs.
  kUnsupported,
};

// The outcome of one write, with a human-readable reason when it failed.
//
// Returned rather than pushed into LayoutTree::diagnostics(), which is cleared
// at the start of every layout pass: a write happens between passes, so a
// rejection parked there would be discarded before anyone looked. The eventual
// C ABI wants an error code out of dg_node_set_prop() anyway.
struct PropWrite {
  PropStatus status = PropStatus::kApplied;

  // Empty when the write succeeded. Otherwise carries the node path, in the
  // form design.md section 5.4.7 asks for.
  std::string message;

  [[nodiscard]] bool ok() const { return status == PropStatus::kApplied; }
};

// One property value, tagged with the type it was built as.
//
// A tagged scalar, matching design.md section 5.9.5's split: gradient, shadow,
// transform and image are NOT representable here and take dedicated setters
// below - `dg::set_gradient()`, `dg::set_shadow()`, `dg::set_transform()`,
// `dg::set_image()`. PropValue has no factory that could ever build a value
// of type `k_gradient`/`k_shadow`/`k_transform`/`k_image` - only `number()`,
// `length()`, `color()` and `option()` exist - so a scalar write against one
// of those four ids always names the wrong entry point. node_props.cpp
// reports `background_gradient`/`shadow`/`image_source` as `kTypeMismatch`
// through this door specifically, because each IS implemented, just not
// here; `transform` still reports `kUnsupported`, because no door yet paints
// one at all - the sentence "which door" only applies once there is a
// working destination behind it.
//
// `length` carries an absolute value only. The table's third length mode - a
// percentage of the incoming max_* constraint - has no constructor here
// because the engine cannot resolve one, and this project does not write a
// representation ahead of the code that would consume it. It arrives with the
// slice that can resolve it.
class PropValue {
 public:
  [[nodiscard]] static constexpr PropValue number(float value) {
    return PropValue{PropType::k_float, value, 0};
  }
  [[nodiscard]] static constexpr PropValue length(float value) {
    return PropValue{PropType::k_length, value, 0};
  }
  [[nodiscard]] static constexpr PropValue color(Color value) {
    return PropValue{PropType::k_color, 0.0F, value.argb()};
  }

  // An index into the property's `values` list. The generated
  // DG_<PROPERTY>_<VALUE> constants are the spellings; the ordinal is what
  // crosses the boundary, so the ORDER of that list is an ABI contract too.
  [[nodiscard]] static constexpr PropValue option(std::uint32_t ordinal) {
    return PropValue{PropType::k_enum, 0.0F, ordinal};
  }

  [[nodiscard]] constexpr PropType type() const { return type_; }

  // Meaningful for k_float and k_length. Reading it after building a colour
  // answers 0, which is why the type is checked before any applier runs
  // rather than inside each one.
  [[nodiscard]] constexpr float scalar() const { return scalar_; }
  [[nodiscard]] constexpr Color as_color() const { return Color::from_argb(bits_); }
  [[nodiscard]] constexpr std::uint32_t ordinal() const { return bits_; }

  friend constexpr bool operator==(const PropValue&, const PropValue&) = default;

 private:
  constexpr PropValue(PropType type, float scalar, std::uint32_t bits)
      : type_(type), scalar_(scalar), bits_(bits) {}

  PropType type_ = PropType::k_float;
  float scalar_ = 0.0F;
  std::uint32_t bits_ = 0;
};

// Writes one property onto one node, and damages exactly what changed.
//
// `prop_id` IS A PLAIN dg_prop_id - that is, a std::uint16_t - and is never
// cast into an enumeration anywhere below. That is the boundary decision this
// slice was asked to make, and it is smaller than the alternatives rather than
// cleverer than them: the set of values a host may send is every uint16_t, so
// a type that claims otherwise would be claiming something untrue the moment
// the C ABI lands. Validation is not a range test followed by a cast; it IS
// the dispatch, whose generated default arm is the single place that decides
// an id is unknown. prop_ids.generated.h records what clang-tidy measured
// against the enumeration form that used to be there.
//
// WHICH INVALIDATION A WRITE COSTS is decided by which struct the property
// lands in, not by a second table that could disagree with the first:
//
//   a paint property writes NodeStyle and calls RenderTree::set_style, which
//   damages the node and does NOT mark layout dirty;
//
//   a layout property writes BoxStyle and calls LayoutTree::set_box, which
//   marks the node dirty up to its relayout boundary; the next layout pass
//   damages both the box the node vacated and the one it moved to, because
//   that is what RenderTree::set_local_bounds already does.
//
// border_width_* is deliberately both, and that is correct rather than a
// hedge: a border occupies layout space and is painted.
//
// A failed write changes NOTHING. The applier works on copies of BoxStyle and
// NodeStyle and only commits them once it has succeeded, so a rejected value
// cannot leave a node half-configured.
PropWrite set_prop(LayoutTree& tree, NodeId node, dg_prop_id prop_id, const PropValue& value);

// --------------------------------------------------------------------------
// The dedicated-setter channel (design.md section 5.9.5).
// --------------------------------------------------------------------------
//
// Four functions, one per complex-typed property, mirroring the shape design
// eventually exports as a C ABI (`dg_node_set_gradient`/`_shadow`/`_image`,
// all three already spelled out in design.md; `_transform` is the omission
// slice 4-10 found and this slice's own verdict is below):
//
//   int dg_node_set_gradient(dg_node_t*, uint16_t prop_id, const dg_gradient_desc*);
//   int dg_node_set_shadow  (dg_node_t*, uint16_t prop_id, const dg_shadow_desc*);
//   int dg_node_set_image   (dg_node_t*, uint16_t prop_id, const dg_image_desc*);
//
// `dg_node_t*` is `LayoutTree&, NodeId` here (the ABI does not exist yet -
// that is phase P5, explicitly out of this slice's scope), and each
// `const dg_..._desc*` is the C++ descriptor type the field beside it already
// carries on `NodeStyle` (`LinearGradientStyle`, `ShadowStyle`) or, for
// `image`, the same `ImageStyle` `RenderTree::set_image()` already accepts
// (slice 5-1). Nothing here invents a SECOND descriptor shape to convert into
// the first - `set_gradient()`/`set_shadow()` write the caller's struct
// straight onto `NodeStyle` after validating it, exactly as the eventual ABI
// wrapper would after copying `*desc` out of C.
//
// WHY FOUR SEPARATE FUNCTIONS RATHER THAN ONE `PropValue` VARIANT: this is
// design.md's own choice, not an invention of this slice - `dg_node_set_*`
// are top-level ABI entry points beside `dg_node_set_prop`, not another
// tagged case inside `dg_value`. A gradient's stop list and a shadow's four
// scalars have no common size, so cramming them into one tagged union would
// need a heap-allocated variant `dg_value` never otherwise carries.
//
// HOW THE ABI LOCK STAYS UNWEAKENED: every one of these four still takes the
// SAME `dg_prop_id` the scalar path does, generated from the SAME
// props/drawgui.props.toml entry, checked here against the SAME
// `dg::prop_type()` the scalar dispatch reads (see complex_prop_prelude() in
// node_props.cpp). No new id, no new lock entry, and no parallel numbering
// scheme was created for this channel - a caller who sends
// `DG_PROP_BACKGROUND_GRADIENT` to `set_shadow()` is rejected by exactly the
// mechanism that already rejects `DG_PROP_WIDTH` sent as a colour.
//
// THE FIRST PROTOTYPE, BUILT BEFORE THE SHAPE WAS EXTRACTED: `set_image()`.
// doc/image.md section 7 named `image_source` as a candidate for either the
// channel's fourth client or its first prototype; this slice picked
// PROTOTYPE, because slice 5-1 had already built and proven the entire
// decode/paint/layout path underneath it (`ImageCatalog`, `carries_image()`,
// `RenderTree::set_image()`) - the only missing piece was the id-based entry
// point itself, which isolates the channel's OWN design (id validation,
// status vocabulary, commit-on-success) from the difficulty of a new visual
// feature. `set_gradient()`/`set_shadow()` were then built AFTER, reusing
// `complex_prop_prelude()` extracted from `set_image()`'s own working code -
// obeying this project's standing rule that an interface is written after at
// least one working implementation, never ahead of one.
//
// `set_transform()` EXISTS AS A FOURTH ENTRY POINT BUT ALWAYS REFUSES.
// design.md section 5.9.5 lists only three dedicated setters - gradient,
// shadow, image - and never states `dg_node_set_transform`'s shape at all,
// which is a genuine gap in the design document itself (4-10 found it; this
// slice is the one instructed to settle it). The verdict: `TransformDesc`
// below IS that shape, decomposed exactly as design.md section 5.9.6 already
// specifies for `transform` ("translate/scale/rotate + origin, for
// interpolation"), so the ABI's eventual signature can be read straight off
// it. What is NOT built is the capability behind it: every rectangle this
// engine tracks is axis-aligned integer device pixels (damage, hit testing,
// clipping all speak `PixelRect`), so a general 2D transform would turn every
// one of those into a quad or a non-invertible-without-care matrix multiply -
// three subsystems that would all have to change together, not one at a
// time. `set_transform()` therefore validates its `prop_id` through the same
// prelude as the other three (so a caller gets a real, consistent answer
// rather than a missing symbol) and then reports `kUnsupported` naming the
// specific blocker, exactly as `dg::set_prop()`'s own `apply_transform` case
// already does for the scalar door. doc/complex-properties.md section 4 is
// the full argument for why this is a decline rather than a half-built
// feature.

// The ABI shape design.md section 5.9.6 already specifies for `transform`:
// translate/scale/rotate decomposed rather than a raw 2x3 matrix, "便于动画
// 插值" (so each component can be interpolated independently once an
// animation clock exists - design.md section 5.16.1, which this project does
// not have). `origin_x`/`origin_y` is the pivot scale and rotate are applied
// around, relative to the node's own content origin.
//
// This struct is NEVER converted into node state: no `NodeStyle` field reads
// it, because `set_transform()` always refuses (see above). It exists so the
// dedicated setter's SIGNATURE - the thing 4-10 found missing from design.md
// - has one concrete, citable shape rather than remaining an unresolved
// question mark.
struct TransformDesc {
  float translate_x = 0.0F;
  float translate_y = 0.0F;
  float scale_x = 1.0F;
  float scale_y = 1.0F;
  float rotate_deg = 0.0F;
  float origin_x = 0.0F;
  float origin_y = 0.0F;
};

// The prototype (see above): a decoded ImageCatalog entry, by id validation
// alone - `image_fit`/`image_placeholder_color` already ride the ordinary
// scalar path (slice 5-1), so only the source itself needs this door.
[[nodiscard]] PropWrite set_image(LayoutTree& tree, NodeId node, dg_prop_id prop_id,
                                  const ImageStyle& image);

// A linear gradient, at least two stops with finite offsets in 0..1 strictly
// increasing (Skia's own `SkGradient::Colors` contract) - doc/complex-
// properties.md section 2 is the validation argument in full.
[[nodiscard]] PropWrite set_gradient(LayoutTree& tree, NodeId node, dg_prop_id prop_id,
                                     const LinearGradientStyle& gradient);

// An outer drop shadow. `blur_radius` is capped - BUDGETED, not merely
// bounds-checked - because doc/cpu-raster-findings.md measured blur at 52% of
// a frame's raster time; doc/complex-properties.md section 3 names the
// number and the reasoning behind it.
[[nodiscard]] PropWrite set_shadow(LayoutTree& tree, NodeId node, dg_prop_id prop_id,
                                   const ShadowStyle& shadow);

// Always reports kUnsupported, naming the specific blocker - see the
// channel-level comment above and doc/complex-properties.md section 4.
[[nodiscard]] PropWrite set_transform(LayoutTree& tree, NodeId node, dg_prop_id prop_id,
                                      const TransformDesc& transform);

}  // namespace dg
