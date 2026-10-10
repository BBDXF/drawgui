// Focus regressions: the old editor's caret after focus moves on, focused()
// naming the component, presses on disabled nodes inside a modal dialog,
// and a dropdown opened above a dialog.

#include <optional>
#include <string>
#include <vector>

#include "runtime_fixture.h"

using runtime_test::of;
using runtime_test::Rt;
namespace ev = dg::events;

namespace {

dg::PixelRect caret_of(Rt& t, dg::NodeId editor) {
  const dg::NodeId caret = t.win->widgets().at(editor).caret;
  return t.win->layout().render().local_bounds(caret);
}

bool caret_hidden(Rt& t, dg::NodeId editor) {
  const dg::PixelRect r = caret_of(t, editor);
  return r.width == 0 || r.height == 0;
}

void click_center(Rt& t, dg::NodeId node) {
  const dg::PixelRect r = t.bounds(node);
  t.press(r.x + (r.width / 2), r.y + (r.height / 2));
  (void)t.pump();
}

void type(Rt& t, dg::NodeId editor, const char* text) {
  REQUIRE(t.win->set_text(editor, DG_PROP_TEXT, text) == dg::Status::kOk);
}

}  // namespace

TEST_SUITE("runtime focus fixes") {
  TEST_CASE("switching editors hides the old caret (click and Tab, field and area)") {
    Rt t;
    const dg::NodeId a = t.add(dg::NodeKind::kTextField);
    const dg::NodeId b = t.add(dg::NodeKind::kTextField);
    const dg::NodeId area = t.add(dg::NodeKind::kTextArea);
    type(t, a, "alpha");
    type(t, b, "beta");
    type(t, area, "gamma");
    (void)t.pump();

    click_center(t, a);
    CHECK(t.win->focused() == a);
    CHECK_FALSE(caret_hidden(t, a));
    click_center(t, b);
    CHECK(t.win->focused() == b);
    CHECK(caret_hidden(t, a));
    CHECK_FALSE(caret_hidden(t, b));
    click_center(t, area);
    CHECK(caret_hidden(t, b));
    CHECK_FALSE(caret_hidden(t, area));
    click_center(t, a);
    CHECK(caret_hidden(t, area));

    t.key(dg::Key::kTab);
    (void)t.pump();
    CHECK(t.win->focused() == b);
    CHECK(caret_hidden(t, a));
    t.key(dg::Key::kTab);
    (void)t.pump();
    CHECK(t.win->focused() == area);
    CHECK(caret_hidden(t, b));
    CHECK_FALSE(caret_hidden(t, area));
  }

  TEST_CASE("switching editors inside a dialog hides the old caret") {
    Rt t;
    const dg::NodeId form = t.add(dg::NodeKind::kBox);
    t.set(form, DG_PROP_DIRECTION, dg::PropValue::option(DG_DIRECTION_COLUMN));
    const dg::NodeId a = t.add(dg::NodeKind::kTextField, form);
    const dg::NodeId area = t.add(dg::NodeKind::kTextArea, form);
    t.set(area, DG_PROP_HEIGHT, dg::PropValue::length(60.0F));
    type(t, a, "alpha");
    type(t, area, "gamma");
    (void)t.pump(1);
    dg::DialogOptions options;
    options.title = "Form";
    options.buttons = {"OK"};
    options.content = form;
    REQUIRE(t.win->open_dialog(options).has_value());
    (void)t.pump();

    // The form's usual start: the app focuses its first field.
    REQUIRE(t.win->focus(a) == dg::Status::kOk);
    (void)t.pump();
    CHECK_FALSE(caret_hidden(t, a));
    click_center(t, area);
    CHECK(t.win->focused() == area);
    CHECK(caret_hidden(t, a));
    CHECK_FALSE(caret_hidden(t, area));
    click_center(t, a);
    CHECK(t.win->focused() == a);
    CHECK(caret_hidden(t, area));
    CHECK_FALSE(caret_hidden(t, a));
    click_center(t, area);
    CHECK(t.win->focused() == area);
    CHECK(caret_hidden(t, a));
    CHECK_FALSE(caret_hidden(t, area));
    // Tab: area -> OK button: the area's caret goes too.
    t.key(dg::Key::kTab);
    (void)t.pump();
    CHECK(t.win->focused() != area);
    CHECK(caret_hidden(t, area));
    // Tab again -> the field: its caret comes back.
    t.key(dg::Key::kTab);
    (void)t.pump();
    CHECK(t.win->focused() == a);
    CHECK_FALSE(caret_hidden(t, a));
  }

  TEST_CASE("focused() names the component; focused_node() the raw node") {
    Rt t;
    const dg::NodeId table = t.add(dg::NodeKind::kTable);
    dg::TableRef ref = t.win->table(table).value();
    REQUIRE(ref.set_columns({dg::TableColumnSpec{"Name", 100, std::nullopt, true}}) == dg::Status::kOk);
    REQUIRE(ref.set_rows({{"one"}, {"two"}}) == dg::Status::kOk);
    (void)t.pump();
    REQUIRE(t.win->focus(table) == dg::Status::kOk);
    (void)t.pump();
    CHECK(t.win->focused() == table);
    REQUIRE(t.win->focused_node().has_value());
    t.win->clear_focus();
    CHECK(!t.win->focused().has_value());
    CHECK(!t.win->focused_node().has_value());
  }

  TEST_CASE("a press on a disabled dialog button keeps focus") {
    Rt t;
    const dg::NodeId form = t.add(dg::NodeKind::kBox);
    const dg::NodeId field = t.add(dg::NodeKind::kTextField, form);
    const dg::NodeId off = t.add(dg::NodeKind::kButton, form);
    t.set(off, DG_PROP_DISABLED, dg::PropValue::boolean(true));
    (void)t.pump(1);
    dg::DialogOptions options;
    options.title = "Form";
    options.buttons = {"OK", "Cancel"};
    options.content = form;
    const std::int32_t id = t.win->open_dialog(options).value();
    (void)t.pump();
    REQUIRE(t.win->focus(field) == dg::Status::kOk);
    REQUIRE(t.win->set_dialog_button_disabled(id, 0, true) == dg::Status::kOk);
    (void)t.pump();
    REQUIRE(t.win->focused() == field);

    // The disabled OK button: find it by its painted text.
    const dg::RenderTree& tree = t.win->layout().render();
    std::optional<dg::NodeId> ok;
    for (const dg::NodeId n : dg::subtree_of(tree, dg::RenderTree::root())) {
      if (tree.style(n).text.text == "OK") {
        ok = n;
      }
    }
    REQUIRE(ok.has_value());
    click_center(t, *ok);
    CHECK(t.win->has_dialog());
    CHECK(t.win->focused() == field);
    CHECK_FALSE(caret_hidden(t, field));
    // A disabled node inside the content: focus stays too.
    click_center(t, off);
    CHECK(t.win->focused() == field);
  }

  TEST_CASE("a dropdown inside dialog content opens, selects and closes; Escape closes it only") {
    Rt t;
    const dg::NodeId form = t.add(dg::NodeKind::kBox);
    const dg::NodeId node = t.add(dg::NodeKind::kDropdown, form);
    dg::DropdownRef dropdown = t.win->dropdown(node).value();
    REQUIRE(dropdown.set_items({"apple", "banana", "cherry"}) == dg::Status::kOk);
    (void)t.pump(1);
    dg::DialogOptions options;
    options.title = "Pick";
    options.buttons = {"OK", "Cancel"};
    options.cancel_button = 1;
    options.content = form;
    REQUIRE(t.win->open_dialog(options).has_value());
    (void)t.pump();
    const int closed = t.count();

    click_center(t, node);
    REQUIRE(t.count() > closed);
    t.key(dg::Key::kDown);
    t.key(dg::Key::kEnter);
    std::vector<dg::Event> got = t.pump();
    const std::vector<ev::ValueChangedEvent> changed = of<ev::ValueChangedEvent>(got);
    REQUIRE(changed.size() == 1);
    CHECK(changed[0].node == node);
    CHECK(of<ev::DialogResultEvent>(got).empty());
    CHECK(t.count() == closed);
    CHECK(t.win->has_dialog());
    CHECK(t.win->focused() == node);

    // Open again and pick "cherry" with the pointer.
    click_center(t, node);
    REQUIRE(t.count() > closed);
    std::optional<dg::NodeId> cherry;
    const dg::RenderTree& tree = t.win->layout().render();
    for (const dg::NodeId n : dg::subtree_of(tree, dg::RenderTree::root())) {
      if (tree.style(n).text.text == "cherry" && tree.absolute_bounds(n).y > t.bounds(node).y) {
        cherry = n;
      }
    }
    REQUIRE(cherry.has_value());
    click_center(t, *cherry);
    CHECK(dropdown.selected().value() == 2);
    CHECK(t.win->has_dialog());
    CHECK(t.count() == closed);

    click_center(t, node);
    REQUIRE(t.count() > closed);
    // Escape closes the dropdown, not the dialog.
    t.key(dg::Key::kEscape);
    got = t.pump();
    CHECK(of<ev::DialogResultEvent>(got).empty());
    CHECK(t.win->has_dialog());
    CHECK(t.count() == closed);
    CHECK(t.win->focused() == node);
    // The dialog's modal trap survived the nested list: Tab stays inside.
    t.key(dg::Key::kTab);
    (void)t.pump();
    CHECK(t.win->focus_state().scope_is_modal());
    REQUIRE(t.win->focused_node().has_value());
    CHECK(t.win->has_dialog());
    // Escape now closes the dialog.
    t.key(dg::Key::kEscape);
    const std::vector<ev::DialogResultEvent> result = of<ev::DialogResultEvent>(t.pump());
    REQUIRE(result.size() == 1);
    CHECK(result[0].button == 1);
  }
}

