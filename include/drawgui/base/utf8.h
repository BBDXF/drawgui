// Decoding UTF-8, one codepoint at a time.
//
// This exists because font fallback is a per-codepoint decision: the question
// "which face draws this?" cannot be asked of a byte. design.md section 5.13.1
// makes UTF-8 the only boundary format, so a decoder is needed here and will be
// needed again by every later consumer of section 5.13.2's three index spaces.
//
// The failure policy is section 5.13.3's: an ill-formed sequence is REPORTED,
// never silently rewritten to U+FFFD. `Utf8Step::valid` is that report. The
// step still advances by one byte so that a caller looping over a hostile
// string always terminates, and so that the number of steps a string produces
// is a property of the string rather than of how broken it is.
//
// Header-only and dependency-free on purpose: it is small, it is hot, and both
// the font catalog and the paint path need it.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dg {

// One decoded position in a UTF-8 string.
struct Utf8Step {
  // Meaningful only when `valid`. An invalid step deliberately leaves this at
  // zero rather than at U+FFFD, so that code which forgets to check `valid`
  // asks about NUL - a codepoint no font is expected to draw - instead of
  // about a replacement character the input never contained.
  char32_t codepoint = 0;

  // Bytes consumed. Always at least one.
  std::size_t length = 1;

  bool valid = false;
};

namespace detail {

constexpr bool is_continuation(unsigned char byte) {
  return (byte & 0xC0U) == 0x80U;
}

}  // namespace detail

// Decodes the sequence starting at `offset`. `offset` must be inside `text`.
//
// Rejects, as section 5.13.3 requires rather than repairs: truncated
// sequences, missing continuation bytes, overlong encodings, surrogate
// codepoints U+D800..U+DFFF, and anything above U+10FFFF.
[[nodiscard]] inline Utf8Step utf8_decode(std::string_view text, std::size_t offset) {
  const std::size_t available = text.size() - offset;
  const auto lead = static_cast<unsigned char>(text[offset]);

  std::size_t length = 0;
  char32_t value = 0;
  char32_t lowest = 0;
  if (lead < 0x80U) {
    return Utf8Step{lead, 1, true};
  }
  if ((lead & 0xE0U) == 0xC0U) {
    length = 2;
    value = lead & 0x1FU;
    lowest = 0x80;
  } else if ((lead & 0xF0U) == 0xE0U) {
    length = 3;
    value = lead & 0x0FU;
    lowest = 0x800;
  } else if ((lead & 0xF8U) == 0xF0U) {
    length = 4;
    value = lead & 0x07U;
    lowest = 0x10000;
  } else {
    // A continuation byte or an 0xF8..0xFF lead: not the start of anything.
    return Utf8Step{0, 1, false};
  }

  if (available < length) {
    return Utf8Step{0, 1, false};
  }
  for (std::size_t i = 1; i < length; ++i) {
    const auto byte = static_cast<unsigned char>(text[offset + i]);
    if (!detail::is_continuation(byte)) {
      return Utf8Step{0, 1, false};
    }
    value = static_cast<char32_t>((value << 6U) | (byte & 0x3FU));
  }

  const bool overlong = value < lowest;
  const bool surrogate = value >= 0xD800 && value <= 0xDFFF;
  const bool too_large = value > 0x10FFFF;
  if (overlong || surrogate || too_large) {
    return Utf8Step{0, 1, false};
  }
  return Utf8Step{value, length, true};
}

}  // namespace dg
