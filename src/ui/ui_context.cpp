#include "drawgui/ui/ui_context.h"

#include <cstdint>

#include "drawgui/render/paragraph.h"
#include "drawgui/theme/token_ids.generated.h"

namespace dg {
namespace {

void read_color(const Theme& theme, ThemeVariant variant, dg_token_id id, Color& out) {
  if (const std::optional<Color> value = theme.color_value(id, variant)) {
    out = *value;
  }
}

void read_int(const Theme& theme, dg_token_id id, int& out) {
  if (const std::optional<int> value = theme.int_value(id)) {
    out = *value;
  }
}

// `base` with `over` laid on top at `amount` (0..1) - the tinted highlight
// and the dimmed body text are derived from existing tokens this way rather
// than needing tokens of their own.
Color mix(Color base, Color over, float amount) {
  const auto channel = [amount](std::uint8_t a, std::uint8_t b) {
    return static_cast<std::uint8_t>(static_cast<float>(a) +
                                     ((static_cast<float>(b) - static_cast<float>(a)) * amount) +
                                     0.5F);
  };
  return Color::rgba(channel(base.red(), over.red()), channel(base.green(), over.green()),
                     channel(base.blue(), over.blue()), base.alpha());
}

}  // namespace

PopupStyle popup_style_from_theme(const Theme& theme, ThemeVariant variant) {
  PopupStyle style;
  read_color(theme, variant, DG_TOKEN_COLOR_SURFACE, style.surface);
  read_color(theme, variant, DG_TOKEN_COLOR_ON_SURFACE, style.on_surface);
  read_color(theme, variant, DG_TOKEN_COLOR_BORDER, style.border);
  read_color(theme, variant, DG_TOKEN_COLOR_PRIMARY_HOVER, style.item_hover);
  read_color(theme, variant, DG_TOKEN_COLOR_ON_PRIMARY, style.on_item_hover);
  read_color(theme, variant, DG_TOKEN_COLOR_PRIMARY_PRESSED, style.item_pressed);
  read_color(theme, variant, DG_TOKEN_COLOR_PRIMARY, style.accent);
  read_color(theme, variant, DG_TOKEN_COLOR_ON_PRIMARY, style.on_accent);
  read_color(theme, variant, DG_TOKEN_COLOR_PRIMARY_HOVER, style.accent_hover);
  read_color(theme, variant, DG_TOKEN_COLOR_PRIMARY_PRESSED, style.accent_pressed);
  read_color(theme, variant, DG_TOKEN_COLOR_ON_DISABLED, style.on_disabled);
  read_color(theme, variant, DG_TOKEN_COLOR_FOCUS_RING, style.focus_ring);
  read_color(theme, variant, DG_TOKEN_COLOR_SCRIM, style.scrim);
  read_int(theme, DG_TOKEN_RADIUS_MD, style.radius);

  // A highlighted menu row / hovered secondary button is a soft primary
  // tint of the surface, not a solid primary block: the text keeps its
  // normal colour, and a menu's highlight never competes with a dialog's
  // default button. Dark surfaces need a stronger tint to read.
  const bool dark = variant == ThemeVariant::kDark;
  style.item_hover = mix(style.surface, style.accent, dark ? 0.24F : 0.12F);
  style.item_pressed = mix(style.surface, style.accent, dark ? 0.34F : 0.22F);
  style.on_item_hover = style.on_surface;
  read_int(theme, DG_TOKEN_SPACE_MD, style.padding);
  return style;
}

int measure_text_width(const FontCatalog* fonts, FontId font, float size,
                       const std::string& text) {
  if (text.empty()) {
    return 0;
  }
  if (fonts != nullptr) {
    TextStyle style;
    style.text = text;
    style.font = font;
    style.size = size;
    style.align = TextAlign::kLeft;
    const Expected<Paragraph, FontError> built = Paragraph::build(*fonts, style, 100000.0F);
    if (built) {
      return static_cast<int>(built.value().caret_x(static_cast<int>(text.size())) + 0.999F);
    }
  }
  return static_cast<int>(static_cast<float>(text.size()) * size * 0.6F);
}

}  // namespace dg
