// The C ABI of window close confirmation and runtime animation:
// dg_window_set_close_confirmation()/dg_window_close(), dg_node_animate(),
// dg_node_set_transition(), dg_animation_cancel(), dg_app_set_reduced_motion().

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/abi/drawgui.h"
#include "drawgui/render/prop_ids.generated.h"

namespace {

struct Fixture {
  dg_app_t* app = nullptr;
  dg_window_t* window = nullptr;
  dg_node_t* root = nullptr;
  dg_node_t* box = nullptr;

  Fixture() {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    app = dg_app_create(nullptr);
    REQUIRE(app != nullptr);
    dg_window_opts opts{};
    opts.size = sizeof(opts);
    opts.title = "abi runtime loop";
    opts.width = 160;
    opts.height = 120;
    opts.background_argb = 0xFF10141CU;
    window = dg_window_create(app, &opts);
    REQUIRE(window != nullptr);
    root = dg_node_create(app, DG_NODE_TYPE_BOX);
    REQUIRE(dg_window_set_root(window, root) == DG_ERR_OK);
    box = dg_node_create(app, DG_NODE_TYPE_BOX);
    REQUIRE(dg_node_insert_before(root, box, nullptr) == DG_ERR_OK);
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  Fixture(Fixture&&) = delete;
  Fixture& operator=(Fixture&&) = delete;
  ~Fixture() { dg_app_destroy(app); }

  std::vector<dg_event> drain() const {
    REQUIRE(dg_wait_events(app, 0) >= 0);
    std::vector<dg_event> events(64);
    const std::int32_t count = dg_poll_events(app, events.data(), 64);
    REQUIRE(count >= 0);
    events.resize(static_cast<std::size_t>(count));
    return events;
  }
};

dg_value number(float value) {
  dg_value out{};
  out.size = sizeof(out);
  out.type = DG_VALUE_FLOAT;
  out.number = value;
  return out;
}

std::vector<dg_event> of_kind(const std::vector<dg_event>& events, std::uint32_t kind) {
  std::vector<dg_event> out;
  for (const dg_event& event : events) {
    if (event.kind == kind) {
      out.push_back(event);
    }
  }
  return out;
}

}  // namespace

TEST_CASE("abi close: dg_window_close closes and reports DG_EVENT_WINDOW_CLOSED") {
  Fixture f;
  CHECK(dg_window_set_close_confirmation(nullptr, 1) == DG_ERR_INVALID_HANDLE);
  CHECK(dg_window_set_close_confirmation(f.window, 1) == DG_ERR_OK);
  CHECK(dg_window_close(f.window) == DG_ERR_OK);
  const std::vector<dg_event> closed = of_kind(f.drain(), DG_EVENT_WINDOW_CLOSED);
  REQUIRE(closed.size() == 1);
  CHECK(closed[0].window == f.window);
  CHECK(dg_window_close(f.window) == DG_ERR_INVALID_HANDLE);
  CHECK(DG_EVENT_CLOSE_REQUESTED == 31U);
}

TEST_CASE("abi animate: reduced motion lands at the next pump with ANIMATION_FINISHED") {
  Fixture f;
  REQUIRE(dg_app_set_reduced_motion(f.app, 1) == DG_ERR_OK);
  const dg_value from = number(0.0F);
  const dg_value to = number(1.0F);
  const std::int32_t id =
      dg_node_animate(f.box, DG_PROP_OPACITY, &from, &to, 5000, DG_CURVE_EASE_IN_OUT);
  REQUIRE(id > 0);
  const std::vector<dg_event> done = of_kind(f.drain(), DG_EVENT_ANIMATION_FINISHED);
  REQUIRE(done.size() == 1);
  CHECK(done[0].node == f.box);
  CHECK(done[0].window == f.window);
  CHECK(done[0].x == id);
  CHECK(dg_animation_cancel(f.window, id) == DG_ERR_UNKNOWN_ID);
}

TEST_CASE("abi animate: cancel, transitions and refusals") {
  Fixture f;
  const dg_value to = number(1.0F);
  const std::int32_t id = dg_node_animate(f.box, DG_PROP_OPACITY, nullptr, &to, 60000,
                                          DG_CURVE_LINEAR);
  REQUIRE(id > 0);
  CHECK(dg_animation_cancel(f.window, id) == DG_ERR_OK);
  CHECK(dg_animation_cancel(f.window, id) == DG_ERR_UNKNOWN_ID);
  CHECK(of_kind(f.drain(), DG_EVENT_ANIMATION_FINISHED).empty());

  CHECK(dg_node_animate(f.box, DG_PROP_OPACITY, nullptr, nullptr, 10, DG_CURVE_LINEAR) ==
        DG_ERR_INVALID_ARGUMENT);
  CHECK(dg_node_animate(f.box, DG_PROP_OPACITY, nullptr, &to, 10, 99) ==
        DG_ERR_VALUE_OUT_OF_RANGE);
  dg_value color{};
  color.size = sizeof(color);
  color.type = DG_VALUE_COLOR;
  color.bits = 0xFF000000U;
  CHECK(dg_node_animate(f.box, DG_PROP_OPACITY, nullptr, &color, 10, DG_CURVE_LINEAR) ==
        DG_ERR_TYPE_MISMATCH);
  dg_node_t* pending = dg_node_create(f.app, DG_NODE_TYPE_BOX);
  CHECK(dg_node_animate(pending, DG_PROP_OPACITY, nullptr, &to, 10, DG_CURVE_LINEAR) ==
        DG_ERR_NO_WINDOW);

  CHECK(dg_node_set_transition(f.box, DG_PROP_OPACITY, 200, DG_CURVE_EASE_OUT) == DG_ERR_OK);
  const dg_value half = number(0.5F);
  CHECK(dg_node_set_prop(f.box, DG_PROP_OPACITY, &half) == DG_ERR_OK);
  CHECK(dg_node_set_transition(f.box, DG_PROP_OPACITY, 0, DG_CURVE_LINEAR) == DG_ERR_OK);
  CHECK(dg_node_set_transition(f.box, DG_PROP_OPACITY, 10, 0) == DG_ERR_VALUE_OUT_OF_RANGE);
  CHECK(dg_animation_cancel(nullptr, 1) == DG_ERR_INVALID_HANDLE);
}
