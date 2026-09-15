#include "cprops_scene.h"

#include <optional>
#include <utility>

#include "drawgui/graphics/canvas.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/box.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/prop_ids.generated.h"

namespace cprops_scene {
namespace {

using dg::BoxStyle;
using dg::Canvas;
using dg::Color;
using dg::EdgeInsets;
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

NodeStyle framed_panel() {
  NodeStyle style;
  style.fill = kPanelFill;
  style.border_color = kFrameEdge;
  style.border_width = dg::BorderWidths::all(1.0F);
  return style;
}

dg::LinearGradientStyle three_stop_gradient() {
  dg::LinearGradientStyle gradient;
  gradient.angle_deg = 0.0F;
  gradient.stops = {
      dg::GradientStop{0.0F, kGradientStart},
      dg::GradientStop{0.5F, kGradientMid},
      dg::GradientStop{1.0F, kGradientEnd},
  };
  return gradient;
}

// Hard-edged on purpose (see cprops_scene.h): blur_radius and spread both
// zero, so the sliver this casts past the panel's own edges is one solid
// colour rather than a blurred gradient - what makes it hand-derivable.
dg::ShadowStyle hard_edged_shadow() {
  dg::ShadowStyle shadow;
  shadow.offset_x = static_cast<float>(kShadowOffsetX);
  shadow.offset_y = static_cast<float>(kShadowOffsetY);
  shadow.blur_radius = 0.0F;
  shadow.spread = 0.0F;
  shadow.color = kShadowColor;
  return shadow;
}

}  // namespace

std::vector<std::uint8_t> halves(int size) {
  std::optional<RasterSurface> surface = RasterSurface::create(size, size);
  if (!surface.has_value()) {
    return {};
  }
  Canvas canvas = surface->canvas();
  const auto half = static_cast<float>(size) / 2.0F;
  canvas.fill_rect(Rect::from_xywh(0, 0, static_cast<float>(size), half), kImageTop);
  canvas.fill_rect(Rect::from_xywh(0, half, static_cast<float>(size), half), kImageBottom);
  return surface->encode_png();
}

Scene build(dg::TreeSpec spec) {
  dg::ImageCatalog images;
  const std::vector<std::uint8_t> png = halves(kImageSourceSize);
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

  // gradient: built through the CHANNEL, not by writing
  // NodeStyle::background_gradient directly.
  handles.gradient_panel = tree.add_child(handles.row, panel_box(220, 110), framed_panel());
  const dg::PropWrite gradient_result = dg::set_gradient(
      tree, handles.gradient_panel, DG_PROP_BACKGROUND_GRADIENT, three_stop_gradient());
  (void)gradient_result;  // examples/16 assumes success; the check verifies it

  // shadow: same channel, same shape.
  handles.shadow_panel = tree.add_child(handles.row, panel_box(120, 120), framed_panel());
  const dg::PropWrite shadow_result =
      dg::set_shadow(tree, handles.shadow_panel, DG_PROP_SHADOW, hard_edged_shadow());
  (void)shadow_result;

  // image: the channel's PROTOTYPE client (node_props.h records why this one
  // was built first). image_fit/image_placeholder_color still ride the
  // ordinary scalar dg::set_prop() path (slice 5-1); only the source needs
  // this door.
  NodeStyle image_style = framed_panel();
  image_style.image.fit = dg::ImageFit::kFill;
  handles.image_panel = tree.add_child(handles.row, panel_box(128, 128), image_style);
  dg::ImageStyle image;
  image.source = source;
  image.fit = dg::ImageFit::kFill;
  const dg::PropWrite image_result =
      dg::set_image(tree, handles.image_panel, DG_PROP_IMAGE_SOURCE, image);
  (void)image_result;

  // transform: the fourth client, always declined - node_props.h and
  // doc/complex-properties.md section 4 record why. Called here so the
  // window's console log and the headless check both read the SAME call
  // rather than two independently-invented ones that could disagree.
  const dg::PropWrite transform_result =
      dg::set_transform(tree, handles.gradient_panel, DG_PROP_TRANSFORM, dg::TransformDesc{});

  tree.layout_full();
  return Scene{std::move(tree), std::move(images), handles, transform_result};
}

}  // namespace cprops_scene
