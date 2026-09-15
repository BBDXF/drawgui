#include "image_scene.h"

#include <optional>
#include <utility>

#include "drawgui/graphics/canvas.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/box.h"

namespace image_scene {
namespace {

using dg::BoxStyle;
using dg::Canvas;
using dg::Color;
using dg::EdgeInsets;
using dg::ImageFit;
using dg::LayoutKind;
using dg::NodeStyle;
using dg::RasterSurface;
using dg::Rect;

BoxStyle panel_box(int width, int height) {
  BoxStyle box;
  box.width = width;
  box.height = height;
  return box;
}

NodeStyle panel_style(dg::ImageId source, ImageFit fit, Color placeholder = Color{}) {
  NodeStyle style;
  style.fill = kFrameFill;
  style.border_color = Color::from_argb(0xFF5A6472);
  style.border_width = dg::BorderWidths::all(1.0F);
  style.image.source = source;
  style.image.fit = fit;
  style.image.placeholder = placeholder;
  return style;
}

}  // namespace

std::vector<std::uint8_t> quadrants(int size) {
  std::optional<RasterSurface> surface = RasterSurface::create(size, size);
  if (!surface.has_value()) {
    return {};
  }
  Canvas canvas = surface->canvas();
  const auto half = static_cast<float>(size) / 2.0F;
  canvas.fill_rect(Rect::from_xywh(0, 0, half, half), kTopLeft);
  canvas.fill_rect(Rect::from_xywh(half, 0, half, half), kTopRight);
  canvas.fill_rect(Rect::from_xywh(0, half, half, half), kBottomLeft);
  canvas.fill_rect(Rect::from_xywh(half, half, half, half), kBottomRight);
  return surface->encode_png();
}

Scene build(dg::TreeSpec spec) {
  dg::ImageCatalog images;
  const std::vector<std::uint8_t> png = quadrants(kSourceSize);
  const dg::Expected<dg::ImageId, dg::ImageError> decoded =
      images.decode(png.data(), png.size());
  const dg::ImageId source = decoded.value_or(dg::ImageId{});

  spec.images = images;
  dg::LayoutTree tree{spec};
  Handles handles;

  const dg::NodeId root = dg::LayoutTree::root();
  BoxStyle row;
  row.kind = LayoutKind::kRow;
  row.gap = 24;
  row.padding = EdgeInsets::all(24);
  handles.row = tree.add_child(root, row, NodeStyle{});

  handles.fill_panel =
      tree.add_child(handles.row, panel_box(192, 96), panel_style(source, ImageFit::kFill));
  handles.contain_panel =
      tree.add_child(handles.row, panel_box(96, 192), panel_style(source, ImageFit::kContain));
  handles.cover_panel =
      tree.add_child(handles.row, panel_box(192, 96), panel_style(source, ImageFit::kCover));
  handles.none_panel =
      tree.add_child(handles.row, panel_box(160, 160), panel_style(source, ImageFit::kNone));
  handles.placeholder_panel =
      tree.add_child(handles.row, panel_box(128, 128),
                     panel_style(dg::ImageId{}, ImageFit::kFill, kPlaceholderColor));

  tree.layout_full();
  return Scene{std::move(tree), std::move(images), source, handles};
}

}  // namespace image_scene
