// The data-controls demo scene: a ProgressBar pair (determinate and
// indeterminate), Tabs, a TreeView and a Table, all from the library
// component layer (include/drawgui/ui/), themed from the builtin theme.

#pragma once

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "drawgui/anim/animation_engine.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/theme/theme_bindings.h"
#include "drawgui/ui/progress_bar.h"
#include "drawgui/ui/table.h"
#include "drawgui/ui/tabs.h"
#include "drawgui/ui/tree_view.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"

namespace data_scene {

inline constexpr int kWidth = 900;
inline constexpr int kHeight = 620;
inline constexpr int kTreeItems = 10000;
inline constexpr int kTableRows = 10000;

struct Scene {
  explicit Scene(const dg::TreeSpec& spec) : layout(spec) {}

  dg::LayoutTree layout;
  dg::WidgetSet widgets;
  dg::ThemeBindings theme_bindings;
  dg::Focus focus;
  dg::AnimationEngine animation;
  dg::ActionScopes action_scopes;
  dg::Interaction interaction;
  std::optional<dg::FontCatalog> fonts;
  dg::FontId font;
  dg::ControlStyle style;
  dg::FocusRing ring;
  dg::Color ring_color;

  dg::ProgressBar determinate;
  dg::ProgressBar indeterminate;
  dg::Tabs tabs;
  dg::TreeView tree_view;
  dg::Table table;
  dg::NodeId status;  // a label reporting the last event
  // The table's source data, which the app sorts on kSortRequested.
  std::vector<std::vector<std::string>> rows;
  int sort_column = -1;
  bool sort_ascending = true;

  [[nodiscard]] dg::ControlContext ctx();
  [[nodiscard]] dg::RenderTree& tree() { return layout.render(); }
};

[[nodiscard]] std::unique_ptr<Scene> build(const std::string& font_dir);

// Lays out, then runs every control's sync() and repaints the focus ring.
void relayout(Scene& scene);

// Routes one event to every control; reports what happened on `status`.
void handle_pointer(Scene& scene, const dg::PointerEvent& event);
void handle_key(Scene& scene, const dg::KeyEvent& event);

// The app's sort: rows ordered by `column`, toggling direction on repeat.
void sort_table(Scene& scene, int column);

void set_status(Scene& scene, const std::string& text);
void update_ring(Scene& scene);

// The headless check (--verify). Returns the process exit code.
int verify(const std::string& font_dir, std::ostream& out);

// One frame to PNG (--dump-png). Returns the process exit code.
int dump_png(const std::string& font_dir, const std::string& path, std::ostream& out);

}  // namespace data_scene
