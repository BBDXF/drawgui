// Named easing curves for dg::AnimationEngine.
//
// design.md section 5.16.1's C signatures carry a `uint16_t curve_id`, the
// same shape as `prop_id`/`token_id`/`action_id` - a plain number crossing an
// eventual FFI boundary rather than a C++ type. That similarity raises the
// question this header's own comment on `AnimationEngine` answers at length:
// should curve_id ride props/drawgui.props.toml's generator/lock machinery
// now, the way `token_id` (slice 6-2) and `action_id` (6-3) are expected to?
//
// DECIDED: NO, NOT YET. The generator exists to keep ONE fact - here, "what
// curves exist and what number names each" - from drifting across SEVERAL
// independent consumers: prop_id already drives a C++ header, a dispatch
// switch, an ABI lock and (eventually) `.d.ts` types and ABI code, which is
// exactly the four-file drift design.md section 5.8 decision 5 names. curve_id
// has exactly ONE consumer today - evaluate_curve() below - because the C ABI
// itself (`dg_animate`'s actual FFI surface) is slice 6-3's job, explicitly
// out of this one's scope. Standing up generator/lock machinery for a table
// with one reader is the inverse of this project's own rule ("an interface is
// written after at least one working implementation, never ahead of one" -
// window_manager.h's own top comment): it would be scaffolding for readers
// that do not exist yet. When 6-3 exports `dg_animate`, curve_id becomes a
// fourth ABI-numbered family alongside prop_id/token_id/action_id and belongs
// in that generator at that point - not before.
//
// So: plain constexpr constants, exactly like a property's enum `values`
// constants (`DG_OVERFLOW_CLIP`, etc.) are today before any property rides
// the generator's ABI lock for enum ordinals - a closed, small, hand-written
// set with one authoritative reader.
#pragma once

#include <cstdint>

namespace dg {

using dg_curve_id = std::uint16_t;

// 0 is reserved, matching DG_PROP_INVALID's convention: a default-constructed
// curve id names nothing rather than accidentally naming linear.
inline constexpr dg_curve_id kCurveInvalid = 0;

inline constexpr dg_curve_id kCurveLinear = 1;
inline constexpr dg_curve_id kCurveEaseIn = 2;
inline constexpr dg_curve_id kCurveEaseOut = 3;
inline constexpr dg_curve_id kCurveEaseInOut = 4;

// t and the return value are both clamped to 0..1 by the caller
// (AnimationEngine::tick() derives t from elapsed/duration, which is already
// clamped) - this function does not re-clamp, so a caller driving it directly
// with an out-of-range t (as an easing-overshoot experiment might) sees the
// unclamped polynomial rather than a silently corrected one.
//
// An unknown curve_id answers linear rather than erroring: a curve is chosen
// at animation-creation time from a fixed, small set this header owns
// entirely, so an unrecognised id can only mean a stale binary reading a
// newer id, and falling back to the identity curve is a safe degradation
// design.md section 5.17.5 would accept - unlike a prop_id, nothing here
// crosses a process boundary that could hand back a value this project does
// not control.
[[nodiscard]] float evaluate_curve(dg_curve_id curve_id, float t);

}  // namespace dg
