// Renders one frame on CPU raster and writes it to a PNG.
//
// This is the P0 acceptance criterion from design.md section 9 in executable
// form: proof that the prebuilt Skia links, that the graphics layer works,
// and that a frame can leave the process as an image.

#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "drawgui/graphics/canvas.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"

int main(int argc, char** argv) {
  const std::string output_path = argc > 1 ? argv[1] : "drawgui-example.png";

  constexpr int kWidth = 480;
  constexpr int kHeight = 320;

  std::optional<dg::RasterSurface> surface = dg::RasterSurface::create(kWidth, kHeight);
  if (!surface) {
    std::cerr << "failed to create a raster surface\n";
    return 1;
  }

  dg::Canvas canvas = surface->canvas();
  canvas.clear(dg::Color::rgba(0x1E, 0x20, 0x26));

  canvas.fill_rrect(dg::Rect::from_xywh(40, 40, 400, 110), dg::Radii::all(16),
                    dg::Color::rgba(0x2E, 0x86, 0xDE));
  canvas.fill_rrect(dg::Rect::from_xywh(40, 170, 190, 110), dg::Radii::all(16),
                    dg::Color::rgba(0x27, 0xAE, 0x60));
  canvas.stroke_rrect(dg::Rect::from_xywh(250, 170, 190, 110), dg::Radii::all(16),
                      dg::Color::rgba(0xE7, 0x4C, 0x3C), 4);

  const std::vector<std::uint8_t> png = surface->encode_png();
  if (png.empty()) {
    std::cerr << "failed to encode PNG\n";
    return 2;
  }

  std::ofstream out(output_path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(png.data()),
            static_cast<std::streamsize>(png.size()));
  if (!out) {
    std::cerr << "failed to write " << output_path << "\n";
    return 3;
  }

  std::cout << "wrote " << output_path << " (" << png.size() << " bytes, " << surface->width()
            << "x" << surface->height() << ")\n";
  return 0;
}
