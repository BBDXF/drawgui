// Golden-image comparison.
//
// design.md section 7: golden-image tests always run on CPU raster, compare
// real pixels with a tolerance, and emit a diff image on failure. The
// retrospective this project inherits from calls out `check(true, ...)` style
// fake tests specifically - so the comparison here decodes both images and
// walks every channel of every pixel. There is no code path in which a
// mismatch reports success.
//
// design.md section 7 also states why this exists at all: a self-drawn GUI
// has no DOM to assert against, so pixel comparison is the only thing that
// catches "changed the layout code, and one corner quietly went wrong".

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dg::testing {

// Decoded pixels, always RGBA8888 premultiplied. Both sides of a comparison
// are decoded through this same path, so the representation only has to be
// self-consistent, not canonical.
struct Image {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;  // width * height * 4

  [[nodiscard]] bool empty() const { return width <= 0 || height <= 0; }
};

// Returns nullopt if the data is not a decodable PNG.
[[nodiscard]] std::optional<Image> decode_png(const std::vector<std::uint8_t>& png);

[[nodiscard]] std::vector<std::uint8_t> encode_png(const Image& image);

struct Comparison {
  bool matched = false;
  std::int64_t differing_pixels = 0;
  int max_channel_delta = 0;
  std::string failure_reason;  // non-empty only when matched is false
  Image diff;                  // populated only when matched is false
};

// Compares pixel by pixel, channel by channel.
//
// `tolerance` is the largest per-channel absolute difference still considered
// equal. For CPU raster it should be 0: design.md section 5.3.2 chose CPU
// raster precisely because its output is deterministic, so any drift is a
// real change and not driver noise. The knob exists so that a future
// cross-platform baseline can loosen it deliberately rather than by accident.
[[nodiscard]] Comparison compare(const Image& baseline, const Image& actual, int tolerance);

[[nodiscard]] std::optional<std::vector<std::uint8_t>> read_file(
    const std::filesystem::path& path);

[[nodiscard]] bool write_file(const std::filesystem::path& path,
                              const std::vector<std::uint8_t>& bytes);

}  // namespace dg::testing
