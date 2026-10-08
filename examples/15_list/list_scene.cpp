#include "list_scene.h"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

#include "drawgui/graphics/canvas.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/box.h"
#include "drawgui/theme/theme.h"
#include "drawgui/theme/theme_loader.h"
#include "drawgui/theme/token_ids.generated.h"

namespace list_scene {
namespace {

using dg::BorderWidths;
using dg::BoxStyle;
using dg::Canvas;
using dg::Color;
using dg::CrossAlign;
using dg::EdgeInsets;
using dg::Expected;
using dg::FontCatalog;
using dg::FontError;
using dg::FontId;
using dg::ImageCatalog;
using dg::ImageError;
using dg::ImageFit;
using dg::ImageId;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::ListSlot;
using dg::NodeId;
using dg::NodeStyle;
using dg::Overflow;
using dg::PixelRect;
using dg::RasterSurface;
using dg::Rect;
using dg::ScrollAxis;
using dg::TextAlign;
using dg::Widget;
using dg::WidgetKind;
using dg::WidgetSet;

constexpr std::array<std::uint32_t, 12> kPalette = {
    0xFF2C3E86, 0xFF2C6E86, 0xFF2C8654, 0xFF6E8C2C, 0xFF8C7A2C, 0xFF8C4B2C,
    0xFF8C2C3E, 0xFF6E2C8C, 0xFF3E2C8C, 0xFF2C4B8C, 0xFF2C868C, 0xFF4B8C2C};

std::vector<std::uint8_t> solid_square(int size, Color color) {
  std::optional<RasterSurface> surface = RasterSurface::create(size, size);
  if (!surface.has_value()) {
    return {};
  }
  Canvas canvas = surface->canvas();
  canvas.fill_rect(Rect::from_xywh(0, 0, static_cast<float>(size), static_cast<float>(size)),
                   color);
  return surface->encode_png();
}

std::string zero_pad(int value, int width) {
  std::string digits = std::to_string(value);
  while (static_cast<int>(digits.size()) < width) {
    digits.insert(digits.begin(), '0');
  }
  return digits;
}

}  // namespace

Color item_fill(int index) {
  const auto bucket = static_cast<std::size_t>(((index % 12) + 12) % 12);
  return Color::from_argb(kPalette[bucket]);
}

std::string item_text(int index) {
  return "item #" + zero_pad(index, 4);
}

ItemImage item_image(int index) {
  switch (((index % 3) + 3) % 3) {
    case 0:
      return ItemImage::kA;
    case 1:
      return ItemImage::kB;
    default:
      return ItemImage::kNone;
  }
}

NodeStyle style_for(int index, FontId font, ImageId image_a, ImageId image_b, bool has_font) {
  NodeStyle style;
  style.fill = item_fill(index);
  style.border_color = Color::from_argb(0xFF20262F);
  style.border_width = BorderWidths::all(1.0F);

  switch (item_image(index)) {
    case ItemImage::kA:
      style.image.source = image_a;
      style.image.fit = ImageFit::kNone;
      break;
    case ItemImage::kB:
      style.image.source = image_b;
      style.image.fit = ImageFit::kNone;
      break;
    case ItemImage::kNone:
      style.image.placeholder = kPlaceholderColor;
      break;
  }

  if (has_font) {
    style.text.text = item_text(index);
    style.text.font = font;
    style.text.size = static_cast<float>(kFontSize);
    style.text.color = Color::from_argb(0xFFE8EDF4);
    style.text.align = TextAlign::kLeft;
    style.text.inset = kThumbSize + 20;
  }
  return style;
}

void refresh(Scene& scene, const std::vector<ListSlot>& slots) {
  const bool has_font = scene.fonts.has_value();
  for (const ListSlot& slot : slots) {
    scene.tree.render().set_style(slot.node, style_for(slot.logical_index, scene.font,
                                                       scene.image_a, scene.image_b, has_font));
  }
}

int logical_index_of(const dg::RenderTree& tree, NodeId node) {
  return tree.local_bounds(node).y / kItemHeight;
}

namespace {

std::pair<std::optional<FontCatalog>, FontId> load_fonts(const std::string& font_dir) {
  FontId font;
  std::optional<FontCatalog> fonts;
  Expected<FontCatalog, FontError> scanned = FontCatalog::scan(font_dir);
  if (scanned) {
    FontCatalog catalog = std::move(scanned).value();
    if (const auto id = catalog.add("DejaVu Sans", false)) {
      font = id.value();
    }
    fonts = std::move(catalog);
  }
  return {std::move(fonts), font};
}

std::pair<ImageCatalog, std::pair<ImageId, ImageId>> load_images() {
  ImageCatalog images;
  const std::vector<std::uint8_t> png_a = solid_square(kThumbSize, kImageAColor);
  const std::vector<std::uint8_t> png_b = solid_square(kThumbSize, kImageBColor);
  const Expected<ImageId, ImageError> decoded_a = images.decode(png_a.data(), png_a.size());
  const Expected<ImageId, ImageError> decoded_b = images.decode(png_b.data(), png_b.size());
  return {images, {decoded_a.value_or(ImageId{}), decoded_b.value_or(ImageId{})}};
}

// The scrollbar look, from the builtin theme's dark variant - the same
// direct token read examples/10_scrolling makes.
dg::ScrollbarStyle scrollbar_tokens() {
  dg::ScrollbarStyle style;
  const Expected<dg::Theme, dg::ThemeLoadError> theme = dg::load_builtin_theme();
  if (!theme.has_value()) {
    return style;
  }
  const dg::Theme& t = theme.value();
  const dg::ThemeVariant dark = dg::ThemeVariant::kDark;
  style.track = t.color_value(DG_TOKEN_COLOR_SCROLLBAR_TRACK, dark);
  style.thumb = t.color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB, dark);
  style.thumb_hover = t.color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB_HOVER, dark);
  style.thumb_pressed = t.color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB_PRESSED, dark);
  style.size = t.int_value(DG_TOKEN_SIZE_SCROLLBAR).value_or(style.size);
  return style;
}

}  // namespace

Scene build(const Options& options) {
  dg::TreeSpec spec = options.spec;

  auto [images, image_ids] = load_images();
  spec.images = images;
  auto [fonts, font] = load_fonts(options.font_dir);
  if (fonts.has_value()) {
    spec.fonts = *fonts;
  }

  LayoutTree tree{spec};
  WidgetSet widgets;
  Handles handles;
  widgets.set_scrollbar_style(scrollbar_tokens());
  handles.gutter = widgets.scrollbar_style().size;

  BoxStyle body_box;
  body_box.kind = LayoutKind::kColumn;
  body_box.gap = 16;
  body_box.padding = EdgeInsets::all(20);
  handles.body = LayoutTree::root();
  tree.set_box(handles.body, body_box);

  BoxStyle list_box;
  list_box.kind = LayoutKind::kLeaf;
  list_box.width = kViewportWidth;
  list_box.height = kViewportHeight;
  list_box.padding.right = handles.gutter;  // the scrollbar's reserved gutter
  NodeStyle list_style;
  list_style.fill = Color::from_argb(0xFF14171C);
  list_style.overflow = Overflow::kClip;
  handles.list = tree.add_child(handles.body, list_box, list_style);

  Widget list_widget;
  list_widget.kind = WidgetKind::kList;
  list_widget.list_axis = ScrollAxis::kVertical;
  list_widget.list_item_count = options.item_count;
  list_widget.list_item_extent = kItemHeight;

  std::vector<NodeId> pool;
  pool.reserve(static_cast<std::size_t>(kPoolSize));
  for (int i = 0; i < kPoolSize; ++i) {
    BoxStyle item_box;
    item_box.width = kViewportWidth - handles.gutter;
    item_box.height = kItemHeight;
    pool.push_back(tree.add_child(handles.list, item_box, NodeStyle{}));
  }
  list_widget.list_pool = pool;
  widgets.attach(handles.list, list_widget);

  // After the pool, so it paints and hit-tests above every item.
  handles.track = tree.add_child(handles.list, BoxStyle{}, NodeStyle{});
  handles.thumb = tree.add_child(handles.track, BoxStyle{}, NodeStyle{});
  widgets.attach_scrollbar(tree.render(), handles.list, handles.track, handles.thumb);

  tree.layout_full();

  Scene scene{std::move(tree),
              std::move(widgets),
              std::move(fonts),
              std::move(images),
              image_ids.first,
              image_ids.second,
              font,
              handles};
  refresh(scene, scene.widgets.list_sync(scene.tree.render(), scene.handles.list, 0));
  sync_scrollbar(scene);
  return scene;
}

void sync_scrollbar(Scene& scene) {
  scene.widgets.sync_scrollbar(scene.tree.render(), scene.handles.list, kViewportHeight);
}

Scene build_baseline(const Options& options) {
  dg::TreeSpec spec = options.spec;

  auto [images, image_ids] = load_images();
  spec.images = images;
  auto [fonts, font] = load_fonts(options.font_dir);
  if (fonts.has_value()) {
    spec.fonts = *fonts;
  }
  const bool has_font = fonts.has_value();

  LayoutTree tree{spec};
  WidgetSet widgets;
  Handles handles;

  BoxStyle body_box;
  body_box.kind = LayoutKind::kColumn;
  body_box.gap = 16;
  body_box.padding = EdgeInsets::all(20);
  handles.body = LayoutTree::root();
  tree.set_box(handles.body, body_box);

  BoxStyle viewport_box;
  viewport_box.kind = LayoutKind::kLeaf;
  viewport_box.scroll_axis = ScrollAxis::kVertical;
  viewport_box.width = kViewportWidth;
  viewport_box.height = kViewportHeight;
  NodeStyle viewport_style;
  viewport_style.fill = Color::from_argb(0xFF14171C);
  viewport_style.overflow = Overflow::kClip;
  handles.list = tree.add_child(handles.body, viewport_box, viewport_style);

  BoxStyle content_box;
  content_box.kind = LayoutKind::kColumn;
  const NodeId content = tree.add_child(handles.list, content_box, NodeStyle{});

  for (int i = 0; i < options.item_count; ++i) {
    BoxStyle item_box;
    item_box.width = kViewportWidth;
    item_box.height = kItemHeight;
    const NodeId item = tree.add_child(
        content, item_box, style_for(i, font, image_ids.first, image_ids.second, has_font));
    (void)item;
  }

  Widget scroll_widget;
  scroll_widget.kind = WidgetKind::kScrollView;
  scroll_widget.scroll_axis = ScrollAxis::kVertical;
  scroll_widget.scroll_content = content;
  widgets.attach(handles.list, scroll_widget);

  tree.layout_full();
  return Scene{std::move(tree),
               std::move(widgets),
               std::move(fonts),
               std::move(images),
               image_ids.first,
               image_ids.second,
               font,
               handles};
}

}  // namespace list_scene
