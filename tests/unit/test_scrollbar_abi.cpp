// DG_PROP_SCROLLBAR through the C ABI: the generated DG_SCROLLBAR_* constants
// are what dg_node_set_prop() accepts, and an unknown ordinal is refused.

#include <cstdint>
#include <cstdlib>
#include <initializer_list>

#include <doctest/doctest.h>

#include "drawgui/abi/drawgui.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/theme/token_ids.generated.h"

namespace {

dg_value enum_value(std::uint32_t ordinal) {
  dg_value value{};
  value.size = sizeof(dg_value);
  value.type = DG_VALUE_ENUM;
  value.bits = ordinal;
  return value;
}

}  // namespace

TEST_SUITE("abi scrollbar") {
  TEST_CASE("dg_node_set_prop(DG_PROP_SCROLLBAR) takes DG_SCROLLBAR_* and refuses the rest") {
    CHECK(DG_PROP_SCROLLBAR == 54);
    CHECK(DG_SCROLLBAR_AUTO == 0U);
    CHECK(DG_SCROLLBAR_ALWAYS == 1U);
    CHECK(DG_SCROLLBAR_NEVER == 2U);
    CHECK(DG_TOKEN_SIZE_SCROLLBAR == 19);

    setenv("SDL_VIDEODRIVER", "dummy", 1);
    dg_app_t* app = dg_app_create(nullptr);
    REQUIRE(app != nullptr);
    dg_window_opts opts{};
    opts.size = sizeof(opts);
    opts.title = "abi scrollbar";
    opts.width = 200;
    opts.height = 100;
    dg_window_t* window = dg_window_create(app, &opts);
    REQUIRE(window != nullptr);
    dg_node_t* root = dg_node_create(app, DG_NODE_TYPE_BOX);
    REQUIRE(dg_window_set_root(window, root) == DG_ERR_OK);

    for (const std::uint32_t mode :
         {DG_SCROLLBAR_NEVER, DG_SCROLLBAR_ALWAYS, DG_SCROLLBAR_AUTO}) {
      const dg_value value = enum_value(mode);
      CHECK(dg_node_set_prop(root, DG_PROP_SCROLLBAR, &value) == DG_ERR_OK);
    }
    const dg_value bad = enum_value(DG_SCROLLBAR_NEVER + 1);
    CHECK(dg_node_set_prop(root, DG_PROP_SCROLLBAR, &bad) == DG_ERR_VALUE_OUT_OF_RANGE);
    dg_app_destroy(app);
  }
}
