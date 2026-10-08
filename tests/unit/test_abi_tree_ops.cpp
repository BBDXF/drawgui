// dg_node_insert_before()/dg_node_remove() as a keyed reconciler drives them:
// positional insert, moving a live node, cycle refusal, and subtree removal
// that invalidates every descendant's handle and leaves nothing behind for
// hit testing or painting. Driven through the real C ABI on a real
// dg_window_t under SDL_VIDEODRIVER=dummy, the same way
// examples/19_c_client's verify mode does.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/abi/drawgui.h"
// DG_PROP_* are C-only macros in drawgui.h; C++ gets them from here.
#include "drawgui/render/prop_ids.generated.h"

namespace {

dg_value length(float number) {
  dg_value value{};
  value.size = sizeof(dg_value);
  value.type = DG_VALUE_LENGTH;
  value.number = number;
  return value;
}

dg_value boolean(std::uint32_t bits) {
  dg_value value{};
  value.size = sizeof(dg_value);
  value.type = DG_VALUE_BOOL;
  value.bits = bits;
  return value;
}

void set_disabled(dg_node_t* node, bool disabled) {
  const dg_value value = boolean(disabled ? 1U : 0U);
  REQUIRE(dg_node_set_prop(node, DG_PROP_DISABLED, &value) == DG_ERR_OK);
}

dg_app_t* create_headless_app() {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  return dg_app_create(nullptr);
}

struct Fixture {
  dg_app_t* app = create_headless_app();
  dg_window_t* window = nullptr;
  dg_node_t* root = nullptr;

  Fixture() {
    REQUIRE(app != nullptr);
    dg_window_opts opts{};
    opts.size = sizeof(opts);
    opts.title = "abi tree ops";
    opts.width = 320;
    opts.height = 240;
    opts.background_argb = 0xFF10141CU;
    window = dg_window_create(app, &opts);
    REQUIRE(window != nullptr);
    root = dg_node_create(app, DG_NODE_TYPE_BOX);
    REQUIRE(dg_window_set_root(window, root) == DG_ERR_OK);
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  Fixture(Fixture&&) = delete;
  Fixture& operator=(Fixture&&) = delete;
  ~Fixture() { dg_app_destroy(app); }

  dg_node_t* box() const {
    dg_node_t* node = dg_node_create(app, DG_NODE_TYPE_BOX);
    REQUIRE(node != nullptr);
    return node;
  }

  // Widths tag the boxes so the dump's child order is readable.
  static void set_width(dg_node_t* node, float width) {
    const dg_value value = length(width);
    REQUIRE(dg_node_set_prop(node, DG_PROP_WIDTH, &value) == DG_ERR_OK);
  }

  void pump() const { REQUIRE(dg_wait_events(app, 0) >= 0); }

  // Widths of root's direct children in order, read from the dump JSON.
  std::vector<int> child_widths() const {
    pump();
    const std::string dump = dg_dump_layout_tree(root);
    const std::size_t children = dump.find("\"children\":[");
    REQUIRE(children != std::string::npos);
    std::vector<int> widths;
    int depth = 0;
    for (std::size_t i = children + 12; i < dump.size(); ++i) {
      if (dump[i] == '{') {
        if (depth == 0) {
          const std::size_t w = dump.find("\"w\":", i);
          widths.push_back(std::atoi(dump.c_str() + w + 4));
        }
        ++depth;
      } else if (dump[i] == '}') {
        --depth;
      } else if (dump[i] == ']' && depth == 0) {
        break;
      }
    }
    return widths;
  }

  bool clicked(dg_node_t* node, std::int32_t x, std::int32_t y) const {
    REQUIRE(dg_debug_warp_pointer(window, x, y) == DG_ERR_OK);
    REQUIRE(dg_debug_post_pointer_button(window, 1, x, y) == DG_ERR_OK);
    REQUIRE(dg_debug_post_pointer_button(window, 0, x, y) == DG_ERR_OK);
    bool hit = false;
    for (int i = 0; i < 10; ++i) {
      dg_event events[8];
      if (dg_wait_events(app, 20) < 0) {
        break;
      }
      const std::int32_t count = dg_poll_events(app, events, 8);
      for (std::int32_t j = 0; j < count; ++j) {
        if (events[j].kind == DG_EVENT_CLICK && events[j].node == node) {
          hit = true;
        }
      }
    }
    return hit;
  }
};

void check_handle_dead(dg_node_t* node) {
  const dg_value width = length(10);
  CHECK(dg_node_set_prop(node, DG_PROP_WIDTH, &width) == DG_ERR_INVALID_HANDLE);
  CHECK(dg_node_remove(node) == DG_ERR_INVALID_HANDLE);
}

}  // namespace

TEST_SUITE("abi_tree_ops") {
  TEST_CASE("insert_before with a ref inserts at that position; a live child is moved") {
    Fixture f;
    dg_node_t* a = f.box();
    dg_node_t* b = f.box();
    dg_node_t* c = f.box();
    REQUIRE(dg_node_insert_before(f.root, a, nullptr) == DG_ERR_OK);
    REQUIRE(dg_node_insert_before(f.root, c, nullptr) == DG_ERR_OK);
    REQUIRE(dg_node_insert_before(f.root, b, c) == DG_ERR_OK);
    Fixture::set_width(a, 10);
    Fixture::set_width(b, 20);
    Fixture::set_width(c, 30);
    CHECK(f.child_widths() == std::vector<int>{10, 20, 30});

    // Reverse by moves only - what a keyed diff emits.
    REQUIRE(dg_node_insert_before(f.root, c, a) == DG_ERR_OK);
    REQUIRE(dg_node_insert_before(f.root, b, a) == DG_ERR_OK);
    CHECK(f.child_widths() == std::vector<int>{30, 20, 10});

    // A moved node keeps its handle and its props.
    REQUIRE(dg_node_insert_before(f.root, c, nullptr) == DG_ERR_OK);
    CHECK(f.child_widths() == std::vector<int>{20, 10, 30});
    Fixture::set_width(c, 31);
    CHECK(f.child_widths() == std::vector<int>{20, 10, 31});
  }

  TEST_CASE("bad refs, cycles and cross-window moves are refused") {
    Fixture f;
    dg_node_t* a = f.box();
    dg_node_t* inner = f.box();
    dg_node_t* pending = f.box();
    REQUIRE(dg_node_insert_before(f.root, a, nullptr) == DG_ERR_OK);
    REQUIRE(dg_node_insert_before(a, inner, nullptr) == DG_ERR_OK);

    CHECK(dg_node_insert_before(f.root, pending, inner) == DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_node_insert_before(f.root, a, a) == DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_node_insert_before(a, a, nullptr) == DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_node_insert_before(inner, a, nullptr) == DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_node_insert_before(a, f.root, nullptr) == DG_ERR_INVALID_ARGUMENT);
    // `pending` stayed pending through the refused insert above.
    CHECK(dg_node_insert_before(f.root, pending, a) == DG_ERR_OK);

    dg_window_opts opts{};
    opts.size = sizeof(opts);
    opts.title = "second";
    opts.width = 100;
    opts.height = 100;
    dg_window_t* second = dg_window_create(f.app, &opts);
    REQUIRE(second != nullptr);
    dg_node_t* other_root = dg_node_create(f.app, DG_NODE_TYPE_BOX);
    REQUIRE(dg_window_set_root(second, other_root) == DG_ERR_OK);
    CHECK(dg_node_insert_before(other_root, a, nullptr) == DG_ERR_ALREADY_ATTACHED);
    CHECK(dg_node_insert_before(f.root, other_root, nullptr) == DG_ERR_ALREADY_ATTACHED);
  }

  TEST_CASE("remove detaches the subtree and invalidates every descendant handle") {
    Fixture f;
    dg_node_t* group = f.box();
    dg_node_t* button = dg_node_create(f.app, DG_NODE_TYPE_BUTTON);
    dg_node_t* checkbox = dg_node_create(f.app, DG_NODE_TYPE_CHECKBOX);
    dg_node_t* keep = f.box();
    // `keep` first: siblings paint (and hit-test) in order, so a later
    // sibling at the same spot would cover the button.
    REQUIRE(dg_node_insert_before(f.root, keep, nullptr) == DG_ERR_OK);
    REQUIRE(dg_node_insert_before(f.root, group, nullptr) == DG_ERR_OK);
    REQUIRE(dg_node_insert_before(group, button, nullptr) == DG_ERR_OK);
    REQUIRE(dg_node_insert_before(button, checkbox, nullptr) == DG_ERR_OK);
    Fixture::set_width(group, 100);
    Fixture::set_width(keep, 50);
    CHECK(f.child_widths() == std::vector<int>{50, 100});
    // The button fills the group (100x64) at the root's top-left, past
    // the checkbox (32x32) and over `keep` (50 wide): (70,50) is its own.
    CHECK(f.clicked(button, 70, 50));

    REQUIRE(dg_node_remove(group) == DG_ERR_OK);
    CHECK(f.child_widths() == std::vector<int>{50});

    for (dg_node_t* gone : {group, button, checkbox}) {
      check_handle_dead(gone);
    }
    const dg_value width = length(10);
    CHECK(dg_node_get_checked(checkbox) == DG_ERR_INVALID_HANDLE);

    // Freed slots get reused by the next nodes; the old handles must not
    // start resolving to them.
    dg_node_t* fresh = dg_node_create(f.app, DG_NODE_TYPE_BUTTON);
    REQUIRE(dg_node_insert_before(f.root, fresh, keep) == DG_ERR_OK);
    CHECK(dg_node_set_prop(button, DG_PROP_WIDTH, &width) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_node_set_prop(fresh, DG_PROP_WIDTH, &width) == DG_ERR_OK);
    CHECK(dg_node_remove(fresh) == DG_ERR_OK);

    // Nothing at the button's old spot answers a click any more.
    CHECK_FALSE(f.clicked(button, 70, 50));
    CHECK(dg_node_insert_before(keep, button, nullptr) == DG_ERR_INVALID_HANDLE);
  }

  TEST_CASE("removing the window root clears it so a new root can be set") {
    Fixture f;
    dg_node_t* child = f.box();
    REQUIRE(dg_node_insert_before(f.root, child, nullptr) == DG_ERR_OK);
    REQUIRE(dg_node_remove(f.root) == DG_ERR_OK);
    CHECK(dg_node_remove(child) == DG_ERR_INVALID_HANDLE);
    dg_node_t* replacement = dg_node_create(f.app, DG_NODE_TYPE_BOX);
    CHECK(dg_window_set_root(f.window, replacement) == DG_ERR_OK);
    f.root = replacement;
    CHECK(f.child_widths().empty());
  }

  TEST_CASE("removing a pending node only invalidates its handle") {
    Fixture f;
    dg_node_t* pending = f.box();
    CHECK(dg_node_remove(pending) == DG_ERR_OK);
    CHECK(dg_node_insert_before(f.root, pending, nullptr) == DG_ERR_INVALID_HANDLE);
  }

  TEST_CASE("DG_PROP_DISABLED takes DG_VALUE_BOOL 0/1 through dg_node_set_prop()") {
    Fixture f;
    dg_node_t* button = dg_node_create(f.app, DG_NODE_TYPE_BUTTON);
    REQUIRE(dg_node_insert_before(f.root, button, nullptr) == DG_ERR_OK);
    const dg_value bad = boolean(2);
    CHECK(dg_node_set_prop(button, DG_PROP_DISABLED, &bad) == DG_ERR_VALUE_OUT_OF_RANGE);
    const dg_value wrong_type = length(1);
    CHECK(dg_node_set_prop(button, DG_PROP_DISABLED, &wrong_type) == DG_ERR_TYPE_MISMATCH);
    CHECK(f.clicked(button, 50, 30));
    set_disabled(button, true);
    CHECK_FALSE(f.clicked(button, 50, 30));
    set_disabled(button, false);
    CHECK(f.clicked(button, 50, 30));
  }

  TEST_CASE("a disabled checkbox neither clicks nor toggles") {
    Fixture f;
    dg_node_t* checkbox = dg_node_create(f.app, DG_NODE_TYPE_CHECKBOX);
    REQUIRE(dg_node_insert_before(f.root, checkbox, nullptr) == DG_ERR_OK);
    set_disabled(checkbox, true);
    CHECK_FALSE(f.clicked(checkbox, 16, 16));
    CHECK(dg_node_get_checked(checkbox) == 0);
    set_disabled(checkbox, false);
    CHECK(f.clicked(checkbox, 16, 16));
    CHECK(dg_node_get_checked(checkbox) == 1);
  }

  TEST_CASE("a disabled container disables the buttons inside it") {
    Fixture f;
    dg_node_t* group = f.box();
    dg_node_t* button = dg_node_create(f.app, DG_NODE_TYPE_BUTTON);
    REQUIRE(dg_node_insert_before(f.root, group, nullptr) == DG_ERR_OK);
    REQUIRE(dg_node_insert_before(group, button, nullptr) == DG_ERR_OK);
    CHECK(f.clicked(button, 50, 30));
    set_disabled(group, true);
    CHECK_FALSE(f.clicked(button, 50, 30));
    set_disabled(group, false);
    CHECK(f.clicked(button, 50, 30));
  }
}
