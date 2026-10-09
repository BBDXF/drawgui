// The window-wide component hooks: by NodeKind before a node exists, by
// std::visit on NodeRecord::state afterwards. The routing and per-window
// popups live in popups.cpp.

#include <optional>
#include <string_view>
#include <variant>

#include "app/components/components.h"
#include "app/internal.h"
#include "drawgui/anim/clock.h"

namespace dg::app_detail::components {

dg::NodeSideTables tables_of(WinState& window) {
  return dg::NodeSideTables{window.widgets,   window.theme_bindings, window.focus,
                            window.animation, window.action_scopes,  window.interaction};
}

const dg::FontCatalog* fonts_of(const AppState& app) {
  return app.fonts.has_value() ? &*app.fonts : nullptr;
}

dg::UiContext ui(WinState& window) {
  AppState& app = window.app;
  dg::PopupStyle style;
  const auto [theme, variant] = theme_for(app);
  if (theme != nullptr) {
    style = dg::popup_style_from_theme(*theme, variant);
  }
  if (!window.popups.has_value()) {
    window.popups.emplace(app.wm);
  }
  return dg::UiContext{*window.popups,
                       window.sdl_window,
                       window.tree.render(),
                       tables_of(window),
                       fonts_of(app),
                       app.default_font,
                       style};
}

dg::ControlStyle control_style(const AppState& app) {
  const auto [theme, variant] = theme_for(app);
  return theme != nullptr ? dg::control_style_from_theme(*theme, variant) : dg::ControlStyle{};
}

dg::ControlContext controls(WinState& window) {
  AppState& app = window.app;
  return dg::ControlContext{window.tree, tables_of(window), fonts_of(app), app.default_font,
                            control_style(app)};
}

void defaults(const WinState& window, NodeKind kind, dg::BoxStyle& box, dg::NodeStyle& style) {
  switch (kind) {
    case NodeKind::kTextField:
    case NodeKind::kTextArea:
      editor_defaults(window.app, kind, box, style);
      return;
    case NodeKind::kScrollView:
      scroll_view_defaults(window, box, style);
      return;
    case NodeKind::kList:
      list_defaults(window, box, style);
      return;
    case NodeKind::kDropdown:
      dropdown_defaults(window.app, box, style);
      return;
    case NodeKind::kProgress:
    case NodeKind::kTabs:
    case NodeKind::kTree:
    case NodeKind::kTable:
      control_defaults(kind, box);
      return;
    case NodeKind::kSlider:
    case NodeKind::kRadio:
      slider_radio_defaults(kind, box, style);
      return;
    case NodeKind::kBox:
    case NodeKind::kButton:
    case NodeKind::kLabel:
    case NodeKind::kCheckbox:
    case NodeKind::kImage:
      basic_defaults(window.app, kind, box, style);
      return;
  }
}

void compose(WinState& window, NodeRecord& record) {
  switch (record.kind) {
    case NodeKind::kTextField:
    case NodeKind::kTextArea:
      compose_editor(window, record);
      return;
    case NodeKind::kScrollView:
      compose_scroll_view(window, record);
      return;
    case NodeKind::kList:
      compose_list(window, record);
      return;
    case NodeKind::kDropdown:
      compose_dropdown(window, record);
      return;
    case NodeKind::kProgress:
      record.state.emplace<ProgressState>();
      build_progress(window, record);
      return;
    case NodeKind::kTabs:
      record.state.emplace<TabsState>();
      build_tabs(window, record);
      return;
    case NodeKind::kTree:
      record.state.emplace<TreeState>();
      build_tree(window, record);
      return;
    case NodeKind::kTable:
      record.state.emplace<TableState>();
      build_table(window, record);
      return;
    case NodeKind::kSlider:
      compose_slider(window, record);
      return;
    case NodeKind::kRadio:
      compose_radio(window, record);
      return;
    case NodeKind::kBox:
    case NodeKind::kButton:
    case NodeKind::kLabel:
    case NodeKind::kCheckbox:
    case NodeKind::kImage:
      compose_basic(window, record);
      return;
  }
}

void tick(WinState& window) {
  const dg::AnimTime now = dg::steady_anim_time();
  dg::UiContext ctx = ui(window);
  (void)window.tooltip.tick(ctx, tooltip_target(window), now);
  for (NodeRecord* record : records_in_order(window)) {
    std::visit([&](auto& state) { tick_state(window, *record, state, now); }, record->state);
  }
}

void sync(WinState& window) {
  for (NodeRecord* record : records_in_order(window)) {
    std::visit([&](auto& state) { sync_state(window, *record, state); }, record->state);
  }
}

void restyle(WinState& window) {
  for (NodeRecord* record : records_in_order(window)) {
    std::visit([&](auto& state) { restyle_state(window, *record, state); }, record->state);
  }
}

std::optional<dg::Status> set_prop(WinState& window, NodeRecord& record, std::uint16_t prop_id,
                                   const dg::PropValue& value) {
  return std::visit(
      [&](auto& state) { return set_prop_state(window, record, state, prop_id, value); },
      record.state);
}

void after_prop(WinState& window, NodeRecord& record, std::uint16_t prop_id) {
  std::visit([&](auto& state) { after_prop_state(window, record, state, prop_id); },
             record.state);
}

std::optional<dg::Status> set_text(WinState& window, NodeRecord& record,
                                   std::string_view text) {
  return std::visit([&](auto& state) { return set_text_state(window, record, state, text); },
                    record.state);
}

dg::NodeId focus_target(const NodeRecord& record) {
  return std::visit([&](const auto& state) { return focus_target_state(record, state); },
                    record.state);
}

}  // namespace dg::app_detail::components
