// The small value types the whole platform interface is spelled in.
//
// Two things live here, and nothing else: the opaque handle template that
// window, display and file-watch identifiers are built from, and the physical
// pixel size that logical-pixel geometry cannot express.
//
// Everything under include/drawgui/platform/ is layer 1 of design.md
// section 4, and constraint C1 says it is the layer that exists so no
// `#ifdef __linux__` ever reaches the core. There is therefore no platform
// conditional, no backend type and no Skia type anywhere in this directory -
// the interface is fixed before the first backend exists precisely so that no
// single platform's API can shape it.

#pragma once

#include <cstdint>
#include <type_traits>

namespace dg {

// An opaque unsigned identifier, distinct per Tag.
//
// A WindowId and a DisplayId are both "a number the backend assigned", and
// without the tag they would be the same type and would silently swap at call
// sites. The tag makes that a compile error, which is the cheapest test this
// project can run against it.
//
// Zero is reserved as the invalid value in every instantiation: a
// default-constructed handle names nothing, which is what WindowDesc::owner
// needs in order to say "no owner" without an extra optional.
//
// Raw is a parameter because some of these cross the C ABI, where the width
// is part of the contract rather than an implementation choice.
template <typename Tag, typename Raw = std::uint32_t>
class Handle {
 public:
  using raw_type = Raw;

  static_assert(std::is_unsigned_v<Raw>, "a handle is an opaque unsigned identifier");

  constexpr Handle() = default;

  // Backends construct handles from whatever their native identifier is, and
  // the ABI constructs them from whatever a host passed in. Explicitly named
  // so that a bare integer can never become a handle by accident - and, unlike
  // a scoped enum, this accepts every value its width can carry, so an id
  // drawgui does not recognize survives the trip instead of being folded onto
  // one it does.
  static constexpr Handle from_raw(Raw raw) { return Handle{raw}; }

  [[nodiscard]] constexpr Raw raw() const { return raw_; }
  [[nodiscard]] constexpr bool is_valid() const { return raw_ != kInvalidRaw; }

  friend constexpr bool operator==(Handle, Handle) = default;

 private:
  static constexpr Raw kInvalidRaw = 0;

  constexpr explicit Handle(Raw raw) : raw_(raw) {}

  Raw raw_ = kInvalidRaw;
};

// A size in physical device pixels.
//
// design.md section 5.4.9 keeps layout in logical pixels (dp) and applies DPI
// scaling once, as a canvas transform at the render root. Physical pixels
// exist in this layer and nowhere above it: they are what a framebuffer is
// actually allocated in, and section 5.15.2 selects the presentation strategy
// from their count. They are integral by nature, which is why this is not
// dg::Size.
struct PixelSize {
  int width = 0;
  int height = 0;

  [[nodiscard]] constexpr bool is_empty() const { return width <= 0 || height <= 0; }

  friend constexpr bool operator==(const PixelSize&, const PixelSize&) = default;
};

}  // namespace dg
