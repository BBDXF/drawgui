// dg::grapheme_boundaries (7-2b): the font-independent grapheme-cluster
// boundary source cursor movement/backspace/delete/click-snapping are built
// on (doc/text-input.md's cross-reference to doc/text-layout.md section 2).
//
// The acceptance shape the task itself names: a ZWJ family emoji, a
// skin-tone-modified emoji and a regional-indicator flag pair each count as
// ONE cluster - the exact three cases 7-1's own smoke test already proved
// at the SkUnicode level, pinned again here at the seam 7-2b's TextField
// editing actually calls.

#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/render/grapheme.h"

TEST_CASE("grapheme_boundaries: an empty string has exactly one boundary") {
  CHECK(dg::grapheme_boundaries("") == std::vector<int>{0});
}

TEST_CASE("grapheme_boundaries: plain ASCII has one boundary per byte") {
  CHECK(dg::grapheme_boundaries("ABC") == std::vector<int>{0, 1, 2, 3});
}

TEST_CASE("grapheme_boundaries: a 3-byte CJK character is one cluster") {
  // U+4E2D, "中" - one codepoint, three UTF-8 bytes, one grapheme cluster.
  CHECK(dg::grapheme_boundaries("\xE4\xB8\xAD") == std::vector<int>{0, 3});
}

TEST_CASE("grapheme_boundaries: a ZWJ family emoji is exactly one cluster") {
  // design.md section 5.10.2's own named example: man+ZWJ+woman+ZWJ+girl+
  // ZWJ+boy, 7 codepoints, 25 bytes (4+3+4+3+4+3+4) - one backspace must
  // remove the whole thing.
  const std::string family_emoji = "\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466";
  REQUIRE(family_emoji.size() == 25);
  CHECK(dg::grapheme_boundaries(family_emoji) == std::vector<int>{0, 25});
}

TEST_CASE("grapheme_boundaries: a skin-tone-modified emoji is one cluster") {
  // U+1F44D (thumbs up, 4 bytes) + U+1F3FD (medium skin tone modifier, 4
  // bytes) - 8 bytes, one cluster.
  const std::string thumbs_up_medium = "\U0001F44D\U0001F3FD";
  REQUIRE(thumbs_up_medium.size() == 8);
  CHECK(dg::grapheme_boundaries(thumbs_up_medium) == std::vector<int>{0, 8});
}

TEST_CASE("grapheme_boundaries: a regional-indicator flag pair is one cluster") {
  // U+1F1E8 U+1F1F3 (regional indicators C, N - the CN flag), 4 bytes each,
  // 8 bytes total, one cluster - not two regional-indicator symbols.
  const std::string flag_cn = "\U0001F1E8\U0001F1F3";
  REQUIRE(flag_cn.size() == 8);
  CHECK(dg::grapheme_boundaries(flag_cn) == std::vector<int>{0, 8});
}

TEST_CASE("grapheme_boundaries: mixed ASCII and multi-cluster text") {
  // "A" (1 byte) + e-acute (2 bytes) + "B" (1 byte) + the ZWJ family emoji
  // (25 bytes) + "C" (1 byte) - boundaries at every cluster start plus the
  // string's own end.
  const std::string mixed =
      "A\xC3\xA9"
      "B"
      "\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466"
      "C";
  REQUIRE(mixed.size() == 30);
  CHECK(dg::grapheme_boundaries(mixed) == std::vector<int>{0, 1, 3, 4, 29, 30});
}
