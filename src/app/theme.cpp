// The active theme: resolving colours from it (else from the builtin theme,
// dark variant), and re-applying it to every open window when it changes.

#include <memory>
#include <optional>
#include <utility>

#include "app/components/components.h"
#include "app/internal.h"
#include "drawgui/theme/theme_loader.h"

namespace dg::app_detail {

std::pair<const dg::Theme*, dg::ThemeVariant> theme_for(const AppState& app) {
  if (app.theme != nullptr) {
    return {app.theme.get(), app.variant};
  }
  static const dg::Expected<dg::Theme, dg::ThemeLoadError> builtin = dg::load_builtin_theme();
  return {builtin.has_value() ? &builtin.value() : nullptr, dg::ThemeVariant::kDark};
}

// DG_PROP_DISABLED's appearance, from the theme tokens color.disabled/
// color.on-disabled: the app's active theme when it has one, else the
// builtin theme (dark variant, matching the runtime's own dark default
// palette). Held once per window (WidgetSet::set_disabled_palette()), so a
// theme change re-resolves it in reapply_theme() instead of leaving stale
// colours on widgets attached earlier.
dg::DisabledPalette disabled_palette_for(const AppState& app) {
  const auto [theme, variant] = theme_for(app);
  if (theme == nullptr) {
    return {};
  }
  return dg::DisabledPalette{theme->color_value(DG_TOKEN_COLOR_DISABLED, variant),
                             theme->color_value(DG_TOKEN_COLOR_ON_DISABLED, variant)};
}

// The scrollbar look (scroll views, lists, text areas) from the theme tokens
// color.scrollbar-* and size.scrollbar, resolved like the disabled palette.
dg::ScrollbarStyle scrollbar_style_for(const AppState& app) {
  const auto [theme, variant] = theme_for(app);
  dg::ScrollbarStyle style;
  if (theme == nullptr) {
    return style;
  }
  style.track = theme->color_value(DG_TOKEN_COLOR_SCROLLBAR_TRACK, variant);
  style.thumb = theme->color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB, variant);
  style.thumb_hover = theme->color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB_HOVER, variant);
  style.thumb_pressed = theme->color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB_PRESSED, variant);
  style.size = theme->int_value(DG_TOKEN_SIZE_SCROLLBAR).value_or(style.size);
  return style;
}

// set_theme()/set_theme_variant()/the token overrides all end here: whichever
// one changed the theme's DATA, the same re-apply against every open
// window's own dg::ThemeBindings is what makes the change live - design.md
// section 5.7.6's hot reload, reusing 6-2's ThemeBindings::apply() unchanged
// rather than inventing per-caller invalidation logic three times over. The
// disabled palette is re-resolved AFTER the bindings, so a disabled node
// whose colour is token-bound keeps its disabled look and remembers the new
// enabled colour for later.
void reapply_theme(AppState& app) {
  if (app.theme == nullptr) {
    return;
  }
  const dg::DisabledPalette palette = disabled_palette_for(app);
  for (const std::unique_ptr<dg::Window>& handle : app.windows) {
    WinState& window = handle->impl();
    if (window.closed) {
      continue;
    }
    window.theme_bindings.apply(window.tree, *app.theme, app.variant);
    window.widgets.set_disabled_palette(palette);
    window.widgets.set_scrollbar_style(scrollbar_style_for(app));
    restyle_editors(window);
    for (const NodeRecord* record : records_in_order(window)) {
      if (record->kind == NodeKind::kList) {
        repaint_list(window, *record);
      }
    }
    components::restyle(window);
    window.widgets.refresh_subtree(window.tree.render(), dg::LayoutTree::root(),
                                   window.interaction);
  }
}

}  // namespace dg::app_detail

namespace dg {

void App::set_theme(std::shared_ptr<const Theme> theme, ThemeVariant variant) {
  state_->theme = std::move(theme);
  state_->variant = variant;
  app_detail::reapply_theme(*state_);
}

Status App::set_theme_variant(ThemeVariant variant) {
  if (state_->theme == nullptr) {
    return Status::kNoActiveTheme;
  }
  state_->variant = variant;
  app_detail::reapply_theme(*state_);
  return Status::kOk;
}

Status App::override_color(std::uint16_t token_id, Color value) {
  if (state_->theme == nullptr) {
    return Status::kNoActiveTheme;
  }
  auto copy = std::make_shared<Theme>(*state_->theme);
  copy->set_color(static_cast<dg_token_id>(token_id), state_->variant, value);
  state_->theme = std::move(copy);
  app_detail::reapply_theme(*state_);
  return Status::kOk;
}

Status App::override_int(std::uint16_t token_id, int value) {
  if (state_->theme == nullptr) {
    return Status::kNoActiveTheme;
  }
  auto copy = std::make_shared<Theme>(*state_->theme);
  copy->set_int(static_cast<dg_token_id>(token_id), value);
  state_->theme = std::move(copy);
  app_detail::reapply_theme(*state_);
  return Status::kOk;
}

bool App::has_theme() const {
  return state_->theme != nullptr;
}

}  // namespace dg
