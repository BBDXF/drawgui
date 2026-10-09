// A headless dg::App with one window and a column root, driven through the
// window manager's own injection (the route the C ABI's dg_debug_* hooks
// take) - the C++ runtime tests' counterpart of the ABI tests' fixtures.

#pragma once

#include <cstdlib>
#include <utility>
#include <variant>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/app/app.h"
#include "drawgui/app/window.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/shortcuts/chord.h"
#include "drawgui/shortcuts/logical_key.generated.h"
#include "drawgui/window/window_manager.h"

namespace runtime_test {

inline dg::App make_app() {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  dg::Expected<dg::App, dg::AppError> created = dg::App::create();
  REQUIRE(created.has_value());
  return std::move(created).value();
}

template <class E>
std::vector<E> of(const std::vector<dg::Event>& events) {
  std::vector<E> out;
  for (const dg::Event& event : events) {
    if (const E* match = dg::event_as<E>(event)) {
      out.push_back(*match);
    }
  }
  return out;
}

struct Rt {
  dg::App app;
  dg::Window* win = nullptr;
  dg::NodeId root;

  explicit Rt(int width = 480, int height = 360) : app(make_app()) {
    REQUIRE(app.add_font(DG_TEST_FONT_DIR, "DgTest Latin").has_value());
    dg::WindowOptions options;
    options.title = "runtime test";
    options.width = width;
    options.height = height;
    dg::Expected<dg::Window*, dg::AppError> opened = app.create_window(options);
    REQUIRE(opened.has_value());
    win = opened.value();
    const dg::Expected<dg::NodeId, dg::Status> made = win->create(dg::NodeKind::kBox);
    REQUIRE(made.has_value());
    root = made.value();
    set(root, DG_PROP_WIDTH, dg::PropValue::length(static_cast<float>(width)));
    set(root, DG_PROP_HEIGHT, dg::PropValue::length(static_cast<float>(height)));
    set(root, DG_PROP_DIRECTION, dg::PropValue::option(DG_DIRECTION_COLUMN));
    (void)pump(2);
  }

  void set(dg::NodeId node, std::uint16_t prop, const dg::PropValue& value) const {
    REQUIRE(win->set_prop(node, prop, value) == dg::Status::kOk);
  }

  dg::NodeId add(dg::NodeKind kind, std::optional<dg::NodeId> parent = std::nullopt) const {
    const dg::Expected<dg::NodeId, dg::Status> made = win->create(kind, parent.value_or(root));
    REQUIRE(made.has_value());
    return made.value();
  }

  std::vector<dg::Event> pump(int rounds = 3) {
    std::vector<dg::Event> all;
    for (int i = 0; i < rounds; ++i) {
      std::vector<dg::Event> got = app.pump(5);
      all.insert(all.end(), got.begin(), got.end());
    }
    return all;
  }

  [[nodiscard]] dg::WindowManager& wm() { return app.window_manager(); }

  [[nodiscard]] int count() const {
    return static_cast<int>(
        dg::subtree_of(win->layout().render(), dg::RenderTree::root()).size());
  }

  [[nodiscard]] dg::PixelRect bounds(dg::NodeId node) const {
    return win->layout().render().absolute_bounds(node);
  }

  void press(int x, int y, dg::PointerButton button = dg::PointerButton::kPrimary,
             int clicks = 0) {
    wm().warp_pointer(win->id(), x, y);
    wm().post_pointer_button(win->id(), true, x, y, button, clicks);
    wm().post_pointer_button(win->id(), false, x, y, button, clicks);
  }

  void key(dg::Key key, bool shift = false) {
    wm().post_key(win->id(), true, key, shift);
    wm().post_key(win->id(), false, key, shift);
  }

  void chord(dg::LogicalKey key, dg::Modifier mods = dg::Modifier::kMod) {
    wm().post_logical_key(win->id(), true, key, mods);
  }
};

}  // namespace runtime_test
