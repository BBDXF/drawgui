// UTF-8 decoding, and specifically the part design.md section 5.13.3 is
// opinionated about: an ill-formed sequence is REPORTED, not repaired.
//
// The reason this has its own file rather than living inside the fallback
// tests is that the repair-instead-of-report failure is invisible downstream.
// A decoder that silently yields U+FFFD produces a font resolution, a glyph
// and a drawn box, and every assertion about the drawing still passes - the
// only place the difference is observable is right here.

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

#include "drawgui/base/utf8.h"

namespace {

struct Decoded {
  std::vector<char32_t> codepoints;
  std::vector<bool> valid;
  std::size_t steps = 0;
};

Decoded decode_all(std::string_view text) {
  Decoded out;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const dg::Utf8Step step = dg::utf8_decode(text, offset);
    out.codepoints.push_back(step.codepoint);
    out.valid.push_back(step.valid);
    offset += step.length;
    ++out.steps;
  }
  return out;
}

}  // namespace

TEST_CASE("ASCII decodes one byte at a time") {
  const Decoded decoded = decode_all("Ab~");
  REQUIRE(decoded.steps == 3);
  CHECK(decoded.codepoints[0] == U'A');
  CHECK(decoded.codepoints[1] == U'b');
  CHECK(decoded.codepoints[2] == U'~');
  CHECK(decoded.valid[0]);
}

TEST_CASE("multi-byte sequences decode to the codepoint they encode") {
  SUBCASE("two bytes") {
    const dg::Utf8Step step = dg::utf8_decode("\xC3\xA9", 0);
    CHECK(step.valid);
    CHECK(step.length == 2);
    CHECK(step.codepoint == 0x00E9);
  }
  SUBCASE("three bytes") {
    const dg::Utf8Step step = dg::utf8_decode("\xE4\xB8\xAD", 0);
    CHECK(step.valid);
    CHECK(step.length == 3);
    CHECK(step.codepoint == 0x4E2D);
  }
  SUBCASE("four bytes") {
    const dg::Utf8Step step = dg::utf8_decode("\xF0\x9F\x98\x80", 0);
    CHECK(step.valid);
    CHECK(step.length == 4);
    CHECK(step.codepoint == 0x1F600);
  }
}

// The whole point of section 5.13.3. Each of these is a sequence that a
// permissive decoder would happily turn into a plausible-looking character.
TEST_CASE("ill-formed sequences are rejected rather than repaired") {
  const struct {
    const char* name;
    std::string bytes;
  } cases[] = {
      {"lone continuation byte", "\x80"},
      {"lead byte with no continuation", std::string("\xC3", 1)},
      {"truncated three-byte sequence", "\xE4\xB8"},
      {"truncated four-byte sequence", "\xF0\x9F\x98"},
      {"second byte is not a continuation", "\xE4\x41\xAD"},
      {"overlong two-byte encoding of NUL", "\xC0\x80"},
      {"overlong three-byte encoding of '/'", "\xE0\x80\xAF"},
      {"surrogate U+D800", "\xED\xA0\x80"},
      {"beyond U+10FFFF", "\xF5\x80\x80\x80"},
      {"0xFF is not a lead byte", "\xFF"},
  };

  for (const auto& item : cases) {
    CAPTURE(item.name);
    const dg::Utf8Step step = dg::utf8_decode(item.bytes, 0);
    CHECK_FALSE(step.valid);

    // Not U+FFFD, and not the value the sequence "meant". A caller that
    // forgets to check `valid` must not receive a character the input did not
    // contain.
    CHECK(step.codepoint == 0);
  }
}

TEST_CASE("an invalid step still advances, so a hostile string terminates") {
  // Given every byte here is invalid on its own, When the whole string is
  // walked, Then the walk produces one step per byte and stops.
  const Decoded decoded = decode_all("\x80\x80\xFF\xC0");
  CHECK(decoded.steps == 4);
  for (bool valid : decoded.valid) {
    CHECK_FALSE(valid);
  }
}

TEST_CASE("a valid sequence after an invalid one is still decoded") {
  const Decoded decoded = decode_all("\xFF\xE4\xB8\xAD");
  REQUIRE(decoded.steps == 2);
  CHECK_FALSE(decoded.valid[0]);
  CHECK(decoded.valid[1]);
  CHECK(decoded.codepoints[1] == 0x4E2D);
}

// dg::sanitize_utf8() (7-2b): the ONE substitution policy this codebase has
// for turning ill-formed bytes into well-formed ones - src/render/
// paragraph_build.cpp's SkParagraph::addText() boundary and
// WidgetSet::text_field_insert()'s editing boundary both call this same
// function rather than each hand-rolling their own loop (doc/text-input.md's
// cross-reference to doc/text-layout.md section 3).
TEST_CASE("sanitize_utf8 leaves well-formed text untouched") {
  CHECK(dg::sanitize_utf8("") == "");
  CHECK(dg::sanitize_utf8("Ab~") == "Ab~");
  CHECK(dg::sanitize_utf8("\xE4\xB8\xAD\xE6\x96\x87") == "\xE4\xB8\xAD\xE6\x96\x87");
}

TEST_CASE("sanitize_utf8 replaces exactly the failed decode steps with U+FFFD") {
  // "A" + a lone continuation byte + "B": one failed step (length 1) between
  // two valid ones, so exactly one U+FFFD (3 bytes) is substituted.
  CHECK(dg::sanitize_utf8(std::string{"A\x80"
                                      "B"}) ==
        "A\xEF\xBF\xBD"
        "B");
  // A truncated three-byte lead with nothing after it: one failed step, one
  // U+FFFD, nothing appended for the (absent) continuation bytes.
  CHECK(dg::sanitize_utf8(std::string{"A\xE0"}) == "A\xEF\xBF\xBD");
  // An overlong two-byte encoding of NUL, rejected as one failed 1-byte step
  // each (utf8_decode's own failure length), so TWO bytes produce TWO
  // U+FFFD, not one - the identical "per failed step, not per maximal
  // invalid run" policy paragraph_build.cpp's own substitution already had.
  CHECK(dg::sanitize_utf8(std::string{"\xC0\x80"}) == "\xEF\xBF\xBD\xEF\xBF\xBD");
}
