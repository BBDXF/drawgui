// Unit tests for the golden-image comparator.
//
// These assertions used to live in `golden_test --self-test`, a hand-rolled
// mode in the golden runner that existed only because there was no unit test
// framework. design.md section 7 names doctest for exactly this, so the
// checks moved here and the flag is gone - one source of truth, and failures
// now report which comparison broke instead of a counter.
//
// design.md section 7 calls out `check(true, ...)` style fake tests as a
// known trap. A golden pipeline whose comparator cannot fail is worse than no
// pipeline, because it produces confidence without evidence. That is what
// this file guards: it feeds the comparator inputs whose correct verdict is
// known, including inputs it must reject.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "golden_image.h"

namespace {

constexpr int kWidth = 8;
constexpr int kHeight = 4;
constexpr std::size_t kMutatedPixel = 5;
constexpr std::size_t kGreenChannel = 1;
constexpr std::uint8_t kBaseLevel = 0x40;
constexpr std::uint8_t kOneLevelBrighter = 0x41;

dg::testing::Image uniform_image() {
  dg::testing::Image image;
  image.width = kWidth;
  image.height = kHeight;
  image.pixels.assign(static_cast<std::size_t>(kWidth) * kHeight * 4, kBaseLevel);
  return image;
}

}  // namespace

TEST_CASE("identical images compare equal") {
  const dg::testing::Image uniform = uniform_image();

  const dg::testing::Comparison result = dg::testing::compare(uniform, uniform, 0);

  CHECK(result.matched);
  CHECK(result.differing_pixels == 0);
}

TEST_CASE("a single-channel, one-level difference is detected at zero tolerance") {
  const dg::testing::Image uniform = uniform_image();
  dg::testing::Image differs_by_one_level = uniform;
  differs_by_one_level.pixels[(kMutatedPixel * 4) + kGreenChannel] = kOneLevelBrighter;

  const dg::testing::Comparison result = dg::testing::compare(uniform, differs_by_one_level, 0);

  CHECK_FALSE(result.matched);
  CHECK(result.differing_pixels == 1);
  CHECK(result.max_channel_delta == 1);

  // Without a diff image a failure tells the reader nothing they can look at.
  CHECK_FALSE(result.diff.empty());
}

TEST_CASE("tolerance absorbs a one-level difference") {
  const dg::testing::Image uniform = uniform_image();
  dg::testing::Image differs_by_one_level = uniform;
  differs_by_one_level.pixels[(kMutatedPixel * 4) + kGreenChannel] = kOneLevelBrighter;

  CHECK(dg::testing::compare(uniform, differs_by_one_level, 1).matched);
}

TEST_CASE("images of different sizes never compare equal") {
  const dg::testing::Image uniform = uniform_image();
  dg::testing::Image shorter_by_one_row = uniform;
  shorter_by_one_row.height = kHeight - 1;
  shorter_by_one_row.pixels.resize(static_cast<std::size_t>(kWidth) * (kHeight - 1) * 4);

  CHECK_FALSE(dg::testing::compare(uniform, shorter_by_one_row, 0).matched);
}

TEST_CASE("a PNG round trip is lossless") {
  const dg::testing::Image uniform = uniform_image();

  const std::vector<std::uint8_t> encoded = dg::testing::encode_png(uniform);
  REQUIRE_FALSE(encoded.empty());

  const std::optional<dg::testing::Image> decoded = dg::testing::decode_png(encoded);
  REQUIRE(decoded.has_value());

  // value_or rather than *decoded: an undecodable PNG then compares against an
  // empty image and fails the comparison, instead of dereferencing nothing.
  CHECK(dg::testing::compare(uniform, decoded.value_or(dg::testing::Image{}), 0).matched);
}
