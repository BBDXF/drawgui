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
// A tagged scalar, matching design.md section 5.9.5's split: gradient, shadow
// and transform are NOT representable here and take dedicated setters, none of
// which exists yet because none of the three is implemented.
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

}  // namespace dg
