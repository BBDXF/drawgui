// dg::Window's component refs driven directly (no C ABI): tabs, trees,
// tables, dropdowns, progress bars, images, and a theme switch.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "app/runtime_debug.h"
#include "drawgui/render/image_catalog.h"
#include "drawgui/theme/theme_package.h"
#include "runtime_fixture.h"

using runtime_test::of;
using runtime_test::Rt;
namespace ev = dg::events;

namespace {

// A checksum of the last painted frame inside `r`.
std::uint64_t region_hash(const dg::Window& window, const dg::PixelRect& r) {
  const dg::RasterSurface* surface = dg::app_detail::painted_surface(window);
  REQUIRE(surface != nullptr);
  const dg::PixelView view = surface->peek_pixels();
  std::uint64_t hash = 1469598103934665603ULL;
  for (int y = r.y; y < r.y + r.height && y < view.height; ++y) {
    for (int x = r.x; x < r.x + r.width && x < view.width; ++x) {
      const std::uint8_t* px =
          view.pixels + (static_cast<std::size_t>(y) * view.row_bytes) + (x * 4);
      for (int c = 0; c < 4; ++c) {
        hash = (hash ^ px[c]) * 1099511628211ULL;
      }
    }
  }
  return hash;
}

}  // namespace

TEST_SUITE("runtime components") {
  TEST_CASE("tabs: TabsRef adds panels, keys select, select() reports nothing") {
    Rt t;
    const dg::NodeId node = t.add(dg::NodeKind::kTabs);
    dg::TabsRef tabs = t.win->tabs(node).value();
    const dg::Expected<dg::NodeId, dg::Status> first = tabs.add("One");
    REQUIRE(first.has_value());
    REQUIRE(tabs.add("Locked", true).has_value());
    REQUIRE(tabs.add("Three").has_value());
    CHECK(t.win->kind_of(first.value()) == dg::NodeKind::kBox);
    (void)t.add(dg::NodeKind::kButton, first.value());
    (void)t.pump(1);
    CHECK(tabs.selected().value() == 0);

    REQUIRE(t.win->focus(node) == dg::Status::kOk);
    t.key(dg::Key::kRight);
    const std::vector<ev::TabSelectEvent> events = of<ev::TabSelectEvent>(t.pump());
    REQUIRE(events.size() == 1);
    CHECK(events[0].index == 2);  // the disabled tab is skipped
    CHECK(events[0].node == node);
    CHECK(tabs.select(1) == dg::Status::kValueOutOfRange);
    CHECK(tabs.select(0) == dg::Status::kOk);
    CHECK(tabs.selected().value() == 0);
    CHECK(of<ev::TabSelectEvent>(t.pump(1)).empty());
    CHECK(t.win->tabs(t.root).error() == dg::Status::kUnsupported);
  }

  TEST_CASE("tree: Right on a collapsed item reports TreeEvent kExpand") {
    Rt t;
    const dg::NodeId node = t.add(dg::NodeKind::kTree);
    dg::TreeRef tree = t.win->tree(node).value();
    std::vector<dg::TreeEntry> items(4);
    items[0].label = "src";
    items[1].label = "a.c";
    items[1].depth = 1;
    items[2].label = "doc";
    items[3].label = "x.md";
    items[3].depth = 1;
    REQUIRE(tree.set_items(items) == dg::Status::kOk);
    std::vector<dg::TreeEntry> bad(1);
    bad[0].depth = 1;
    CHECK(tree.set_items(bad) == dg::Status::kInvalidArgument);
    (void)t.pump(1);

    REQUIRE(tree.select(0) == dg::Status::kOk);
    REQUIRE(t.win->focus(node) == dg::Status::kOk);
    t.key(dg::Key::kRight);
    std::vector<ev::TreeEvent> events = of<ev::TreeEvent>(t.pump());
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == ev::TreeEvent::Kind::kExpand);
    CHECK(events[0].item == 0);
    CHECK(events[0].node == node);
    t.key(dg::Key::kLeft);
    events = of<ev::TreeEvent>(t.pump());
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == ev::TreeEvent::Kind::kCollapse);
    CHECK(tree.set_expanded(2, true) == dg::Status::kOk);
    CHECK(tree.set_expanded(4, true) == dg::Status::kValueOutOfRange);
    CHECK(tree.select(-1) == dg::Status::kOk);
    CHECK(tree.selected().value() == -1);
  }

  TEST_CASE("table: a sortable header click is a TableEvent kSort request") {
    Rt t;
    const dg::NodeId node = t.add(dg::NodeKind::kTable);
    dg::TableRef table = t.win->table(node).value();
    std::vector<dg::TableColumnSpec> columns(3);
    columns[0].title = "Name";
    columns[0].width_percent = 50.0F;
    columns[1].title = "Size";
    columns[1].width_px = 80;
    columns[2].title = "Flag";
    columns[2].sortable = false;
    REQUIRE(table.set_columns(columns) == dg::Status::kOk);
    CHECK(table.column_count().value() == 3);
    std::vector<std::vector<std::string>> rows;
    for (int i = 0; i < 20; ++i) {
      rows.push_back({"row " + std::to_string(i), std::to_string(i), "-"});
    }
    REQUIRE(table.set_rows(rows) == dg::Status::kOk);
    (void)t.pump(1);

    t.press(200, 13);  // the Size header
    const std::vector<ev::TableEvent> sort = of<ev::TableEvent>(t.pump());
    REQUIRE(sort.size() == 1);
    CHECK(sort[0].kind == ev::TableEvent::Kind::kSort);
    CHECK(sort[0].index == 1);
    t.press(340, 13);  // Flag is not sortable
    CHECK(of<ev::TableEvent>(t.pump()).empty());
    CHECK(table.set_sort_indicator(1, false) == dg::Status::kOk);
    CHECK(table.set_sort_indicator(3, false) == dg::Status::kValueOutOfRange);
    CHECK(table.select(19) == dg::Status::kOk);
    CHECK(table.selected().value() == 19);
    CHECK(table.set_columns({}) == dg::Status::kInvalidArgument);
  }

  TEST_CASE("dropdown: removing it while open closes its popup") {
    Rt t;
    const int baseline = t.count();
    const dg::NodeId node = t.add(dg::NodeKind::kDropdown);
    dg::DropdownRef dropdown = t.win->dropdown(node).value();
    REQUIRE(dropdown.set_items({"apple", "banana", "cherry"}) == dg::Status::kOk);
    (void)t.pump(1);
    const int closed = t.count();
    t.press(100, 16);
    (void)t.pump(1);
    CHECK(t.count() > closed);
    t.key(dg::Key::kDown);
    t.key(dg::Key::kEnter);
    const std::vector<ev::ValueChangedEvent> changed = of<ev::ValueChangedEvent>(t.pump());
    REQUIRE(changed.size() == 1);
    CHECK(changed[0].selected == dropdown.selected().value());
    CHECK(t.count() == closed);

    t.press(100, 16);
    (void)t.pump(1);
    REQUIRE(t.count() > closed);
    REQUIRE(t.win->remove(node) == dg::Status::kOk);
    (void)t.pump(1);
    CHECK(t.count() == baseline);
    CHECK(dropdown.select(0) == dg::Status::kInvalidNode);
  }

  TEST_CASE("progress: ProgressRef value and indeterminate") {
    Rt t;
    dg::ProgressRef bar = t.win->progress(t.add(dg::NodeKind::kProgress)).value();
    CHECK(bar.set_value(0.25F) == dg::Status::kOk);
    CHECK(bar.value().value() == doctest::Approx(0.25F));
    CHECK(bar.set_value(2.0F) == dg::Status::kValueOutOfRange);
    CHECK(bar.set_indeterminate(true) == dg::Status::kOk);
    CHECK(bar.indeterminate().value());
    (void)t.pump(1);
  }

  TEST_CASE("images: set_image shows a decoded image; other kinds refuse it") {
    Rt t;
    const dg::NodeId image = t.add(dg::NodeKind::kImage);
    const std::string svg =
        "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'>"
        "<rect width='16' height='16' fill='black'/></svg>";
    const dg::Expected<dg::ImageId, dg::ImageError> decoded = t.app.images().decode_any(
        reinterpret_cast<const std::uint8_t*>(svg.data()), svg.size());
    REQUIRE(decoded.has_value());
    CHECK(t.win->set_image(image, decoded.value()) == dg::Status::kOk);
    (void)t.pump(1);
    const dg::NodeId button = t.add(dg::NodeKind::kButton);
    CHECK(t.win->set_image(button, decoded.value()) == dg::Status::kUnsupported);
  }

  TEST_CASE("a theme variant switch restyles a text area and a list") {
    Rt t;
    dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
        dg::ThemePackage::open(DG_TEST_FIXTURE_DIR "/../../themes/builtin");
    REQUIRE(package.has_value());
    dg::Expected<dg::Theme, dg::ThemeLoadError> theme = package.value().load_theme_json();
    REQUIRE(theme.has_value());
    t.app.set_theme(std::make_shared<dg::Theme>(std::move(theme).value()),
                    dg::ThemeVariant::kLight);
    const dg::NodeId area = t.add(dg::NodeKind::kTextArea);
    const dg::NodeId list = t.add(dg::NodeKind::kList);
    REQUIRE(t.win->list(list).value().set_items({"one", "two"}) == dg::Status::kOk);
    (void)t.pump();
    const std::uint64_t light_area = region_hash(*t.win, t.bounds(area));
    const std::uint64_t light_list = region_hash(*t.win, t.bounds(list));

    REQUIRE(t.app.set_theme_variant(dg::ThemeVariant::kDark) == dg::Status::kOk);
    (void)t.pump();
    CHECK(region_hash(*t.win, t.bounds(area)) != light_area);
    CHECK(region_hash(*t.win, t.bounds(list)) != light_list);
  }
}
