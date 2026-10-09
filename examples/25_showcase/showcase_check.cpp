// --verify-showcase: every tab of the showcase driven with injected input
// (pointer, keys, text, clipboard chords) on SDL's dummy video driver, the
// app reacting to the events exactly as in the window mode.

#include "showcase_check.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "drawgui/app/window.h"
#include "drawgui/base/cursor.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/image_catalog.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/chord.h"
#include "drawgui/shortcuts/logical_key.generated.h"
#include "drawgui/widget/widget_set.h"
#include "example_app.h"
#include "showcase.h"

namespace showcase {
namespace {

using dg::NodeId;
using Clock = std::chrono::steady_clock;

std::size_t utf8_length(const std::string& text) {
  std::size_t n = 0;
  for (const char c : text) {
    if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U) {
      ++n;
    }
  }
  return n;
}

class Check {
 public:
  Check(dg::App& app, Showcase& demo, std::ostream& out)
      : app_(app), demo_(demo), w_(demo.window()), drive_(app, w_), out_(out) {}

  int run() {
    feed(drive_.pump());
    theme();
    basics();
    text();
    layout();
    data();
    popups();
    images();
    window();
    out_ << "\n" << (ok_ ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED") << "\n";
    return ok_ ? 0 : 1;
  }

 private:
  void check(bool pass, const std::string& what) { example::check(pass, what, out_, ok_); }

  std::vector<dg::Event> feed(std::vector<dg::Event> events) {
    for (const dg::Event& e : events) {
      demo_.on_event(e);
    }
    return events;
  }
  std::vector<dg::Event> click(NodeId node) { return feed(drive_.click(node)); }
  std::vector<dg::Event> key(dg::Key k) { return feed(drive_.key(k)); }
  std::vector<dg::Event> chord(dg::LogicalKey k) {
    app_.window_manager().post_logical_key(w_.id(), true, k, dg::Modifier::kMod);
    app_.window_manager().post_logical_key(w_.id(), false, k, dg::Modifier::kMod);
    return feed(drive_.pump());
  }
  std::vector<dg::Event> type(const std::string& text) {
    app_.window_manager().post_text_input(w_.id(), text);
    return feed(drive_.pump());
  }
  void tab(int index) {
    demo_.select_tab(index);
    feed(drive_.pump());
  }
  // Pumps (with real waiting) until `done` or `ms` pass.
  template <class F>
  bool wait_for(F done, int ms) {
    const Clock::time_point end = Clock::now() + std::chrono::milliseconds(ms);
    while (!done() && Clock::now() < end) {
      feed(app_.pump(20));
    }
    return done();
  }
  [[nodiscard]] std::string label_text(NodeId node) const {
    return w_.layout().render().style(node).text.text;
  }
  [[nodiscard]] dg::PixelRect bounds(NodeId node) const { return drive_.bounds(node); }
  [[nodiscard]] dg::Cursor cursor_over(NodeId node) {
    const dg::PixelRect r = bounds(node);
    feed(drive_.move(r.x + (r.width / 2), r.y + (r.height / 2)));
    return app_.window_manager().current_cursor(w_.id()).value_or(dg::Cursor::kAuto);
  }

  void theme() {
    out_ << "header: the theme switch re-colours the whole app live\n";
    const Nodes& n = demo_.nodes();
    const dg::RenderTree& tree = w_.layout().render();
    const dg::Color page_before = tree.style(n.root).fill;
    const dg::Color text_before = tree.style(n.click_count).text.color;
    const dg::Color card_before = tree.style(w_.layout().render().parent(n.click_count)).fill;
    click(n.theme_button);
    check(demo_.variant() == dg::ThemeVariant::kDark, "one click switches to dark");
    check(tree.style(n.root).fill == demo_.color_of(Role::kPage) &&
              tree.style(n.root).fill != page_before,
          "the page background takes the dark palette");
    check(tree.style(w_.layout().render().parent(n.click_count)).fill != card_before,
          "a theme-token-bound card surface changes too");
    check(tree.style(n.click_count).text.color != text_before, "muted text changes colour");
    check(w_.widgets().at(n.primary_button).fill_normal !=
              w_.widgets().at(n.secondary_button).fill_normal,
          "buttons are re-coloured from the new theme (primary differs from secondary)");
    check(demo_.status_text() == "主题：深色", "the status line names the event");
    click(n.theme_button);
    check(demo_.variant() == dg::ThemeVariant::kLight && tree.style(n.root).fill == page_before,
          "a second click returns to light");
  }

  void basics() {
    out_ << "\n基础控件: buttons, radios, disabled group, slider, cursors\n";
    tab(kBasics);
    const Nodes& n = demo_.nodes();
    const std::vector<dg::Event> clicked = click(n.primary_button);
    check(example::events_of<dg::events::ClickEvent>(clicked).size() == 1 &&
              demo_.clicks() == 1 && label_text(n.click_count) == "已点击 1 次",
          "a click on 主要操作 counts and updates the label");
    check(demo_.status_text() == "点击：主要操作", "the status line shows the click");
    check(example::events_of<dg::events::ClickEvent>(click(n.disabled_button)).empty(),
          "the disabled button reports no click");
    click(n.size_radios[2]);
    check(w_.checked(n.size_radios[2]).value() && !w_.checked(n.size_radios[1]).value() &&
              label_text(n.size_label) == "字号：大",
          "radio 大 selects and clears 中 (one group)");
    click(n.group_check);
    check(!w_.checked(n.group_check).value(), "the disabled group ignores a click");
    click(n.enable_switch);
    click(n.group_check);
    check(w_.checked(n.group_check).value(), "after 允许编辑 the group takes clicks");
    click(n.enable_switch);
    check(w_.focused() != n.group_check,
          "switching it off again leaves nothing focused inside");
    const dg::PixelRect track = bounds(n.volume);
    const std::vector<dg::events::SliderEvent> moved =
        example::events_of<dg::events::SliderEvent>(
            feed(drive_.click(track.x + track.width - 2, track.y + (track.height / 2))));
    check(!moved.empty() && label_text(n.volume_label) == "100",
          "a press at the slider's end moves it to 100 and the label follows");
    check(cursor_over(n.cursor_tiles[0]) == dg::Cursor::kPointer &&
              cursor_over(n.cursor_tiles[1]) == dg::Cursor::kText &&
              cursor_over(n.cursor_tiles[2]) == dg::Cursor::kCrosshair &&
              cursor_over(n.cursor_tiles[6]) == dg::Cursor::kResizeEw,
          "hovering the tiles sets pointer / text / crosshair / resize cursors");
  }

  void text() {
    out_ << "\n文本: placeholder, typing, max length, clipboard, undo, wrapping\n";
    tab(kText);
    const Nodes& n = demo_.nodes();
    click(n.name_field);
    check(w_.focused() == n.name_field,
          "a click on the placeholder focuses the field under it");
    type("小明 Ming");
    check(
        w_.text(n.name_field).value() == "小明 Ming" && label_text(n.name_placeholder).empty(),
        "typed text arrives and the placeholder hides");
    chord(dg::LogicalKey::kA);
    chord(dg::LogicalKey::kC);
    check(app_.window_manager().get_clipboard_text() == "小明 Ming", "Mod+A, Mod+C copies it");
    click(n.code_field);
    key(dg::Key::kEnd);
    chord(dg::LogicalKey::kV);
    check(w_.text(n.code_field).value() == "DG-2026小明 Mi",
          "Mod+V pastes, cut off at the 12-character limit");
    type("XYZ");
    check(utf8_length(w_.text(n.code_field).value()) == kFieldMaxLength,
          "typing past the limit is refused");
    check(label_text(n.code_counter) ==
              std::to_string(w_.text(n.code_field).value().size()) + " / 12",
          "the counter label follows each edit");
    chord(dg::LogicalKey::kZ);
    check(w_.text(n.code_field).value() == "DG-2026", "Mod+Z undoes the paste");
    const std::string area = w_.text(n.text_area).value();
    check(area.find("🎨") != std::string::npos && area.find("English") != std::string::npos,
          "the text area holds CJK, Latin and emoji");
    check(w_.layout().render().style(n.paragraph).text.wrap, "the paragraph label wraps");
  }

  void layout() {
    out_ << "\n布局: justify modes, percentage widths, wrapping\n";
    tab(kLayout);
    const Nodes& n = demo_.nodes();
    const dg::RenderTree& tree = w_.layout().render();
    const auto chips = [&](NodeId strip) {
      std::vector<dg::PixelRect> out;
      for (const NodeId c : tree.children(strip)) {
        out.push_back(bounds(c));
      }
      return out;
    };
    const dg::PixelRect between_strip = bounds(n.justify_rows[3]);
    const std::vector<dg::PixelRect> between = chips(n.justify_rows[3]);
    check(between.size() == 3 && between.front().x == between_strip.x &&
              between.back().x + between.back().width == between_strip.x + between_strip.width,
          "space_between: first chip flush left, last flush right");
    const dg::PixelRect evenly_strip = bounds(n.justify_rows[5]);
    const std::vector<dg::PixelRect> evenly = chips(n.justify_rows[5]);
    const int g0 = evenly[0].x - evenly_strip.x;
    const int g1 = evenly[1].x - (evenly[0].x + evenly[0].width);
    const int g3 = evenly_strip.x + evenly_strip.width - (evenly[2].x + evenly[2].width);
    check(std::abs(g0 - g1) <= 1 && std::abs(g1 - g3) <= 1 && g0 > 0,
          "space_evenly: equal space before, between and after");
    const std::vector<dg::PixelRect> start = chips(n.justify_rows[0]);
    const std::vector<dg::PixelRect> end = chips(n.justify_rows[2]);
    check(start[0].x == bounds(n.justify_rows[0]).x &&
              end[2].x + end[2].width ==
                  bounds(n.justify_rows[2]).x + bounds(n.justify_rows[2]).width,
          "start hugs the left edge, end the right");
    const int row_width = bounds(n.percent_row).width;
    check(std::abs(bounds(n.percent_cells[1]).width - (row_width / 2)) <= 1 &&
              std::abs(bounds(n.percent_cells[0]).width - (row_width / 4)) <= 1,
          "50% / 25% cells measure half / a quarter of the row");
    const std::vector<NodeId> tags = tree.children(n.wrap_box);
    check(!tags.empty() && bounds(tags.back()).y > bounds(tags.front()).y,
          "the wrap container moves the overflowing tags to a second line");
  }

  void data() {
    out_ << "\n数据: 1000-item list, tree, sortable table, timer-driven progress\n";
    tab(kData);
    const Nodes& n = demo_.nodes();
    const dg::RenderTree& tree = w_.layout().render();
    check(tree.children(n.list).size() < 40,
          "the 1000-item list builds only a viewport of rows (" +
              std::to_string(tree.children(n.list).size()) + " children)");
    const dg::PixelRect list = bounds(n.list);
    const std::vector<dg::events::ListSelectEvent> picked =
        example::events_of<dg::events::ListSelectEvent>(
            feed(drive_.click(list.x + 40, list.y + 45)));
    check(picked.size() == 1 && picked[0].index == 1 &&
              demo_.status_text() == "列表：选中第 2 项",
          "a click on the second row selects it");
    feed(drive_.wheel(list.x + 40, list.y + 45, -30.0F));
    check(tree.children(n.list).size() < 40, "scrolling adds no rows");
    (void)w_.focus(n.tree);
    feed(drive_.pump());
    key(dg::Key::kDown);
    key(dg::Key::kHome);
    const std::vector<dg::events::TreeEvent> collapsed =
        example::events_of<dg::events::TreeEvent>(key(dg::Key::kLeft));
    check(!collapsed.empty() && collapsed.back().kind == dg::events::TreeEvent::Kind::kCollapse,
          "Left on the first folder collapses it");
    const dg::PixelRect table = bounds(n.table);
    const int price_x = table.x + (table.width * 30 / 100) + 20;
    const std::vector<dg::events::TableEvent> sort =
        example::events_of<dg::events::TableEvent>(feed(drive_.click(price_x, table.y + 10)));
    check(!sort.empty() && sort.back().kind == dg::events::TableEvent::Kind::kSort &&
              sort.back().index == 1 && demo_.rows().front()[1] == "3",
          "a click on 单价 asks for a sort; the app sorts ascending");
    feed(drive_.click(price_x, table.y + 10));
    check(std::stoi(demo_.rows().front()[1]) > std::stoi(demo_.rows().back()[1]) &&
              demo_.rows().back()[1] == "3",
          "a second click sorts descending");
    demo_.start_timers();
    const float before = demo_.progress_value();
    check(wait_for([&] { return demo_.progress_value() > before + 0.02F; }, 3000),
          "the repeating timer advances the progress bar");
    check(label_text(n.progress_label) != "0%", "...and its percentage label");
    check(w_.progress(n.spinner).value().indeterminate().value(),
          "the second bar is indeterminate");
  }

  void popups() {
    out_ << "\n弹层: context menu, dialogs, dropdown, tooltip - node count stays bounded\n";
    tab(kPopups);
    (void)app_.set_tooltip_delay(0);
    const Nodes& n = demo_.nodes();
    feed(drive_.move(5, 5));
    const int baseline = w_.node_count();
    const dg::PixelRect area = bounds(n.menu_area);
    const auto right_click = [&] {
      dg::WindowManager& wm = app_.window_manager();
      const int x = area.x + 30;
      const int y = area.y + 20;
      wm.warp_pointer(w_.id(), x, y);
      wm.post_pointer_button(w_.id(), true, x, y, dg::PointerButton::kSecondary);
      wm.post_pointer_button(w_.id(), false, x, y, dg::PointerButton::kSecondary);
      feed(drive_.pump());
      feed(drive_.pump());
    };
    right_click();
    check(demo_.last_menu().has_value() && w_.node_count() > baseline,
          "a right-click opens the context menu");
    const std::vector<dg::events::MenuEvent> dismissed =
        example::events_of<dg::events::MenuEvent>(key(dg::Key::kEscape));
    check(dismissed.size() == 1 && !dismissed[0].item.has_value(), "Escape dismisses it");
    right_click();
    key(dg::Key::kDown);
    key(dg::Key::kEnter);
    check(label_text(n.menu_label) == "选择了「粘贴」",
          "Down + Enter picks the second entry (粘贴)");

    click(n.delete_button);
    check(w_.has_dialog(), "删除文件… opens a modal dialog");
    std::vector<dg::events::DialogResultEvent> result =
        example::events_of<dg::events::DialogResultEvent>(key(dg::Key::kEnter));
    check(result.size() == 1 && result[0].button == 0 && !w_.has_dialog(),
          "Enter reports the default button 0");
    click(n.delete_button);
    result = example::events_of<dg::events::DialogResultEvent>(key(dg::Key::kEscape));
    check(result.size() == 1 && result[0].button == 1, "Escape reports the cancel button 1");

    click(n.dropdown);
    check(w_.node_count() > baseline, "a click opens the dropdown");
    key(dg::Key::kDown);
    const std::vector<dg::events::ValueChangedEvent> chosen =
        example::events_of<dg::events::ValueChangedEvent>(key(dg::Key::kEnter));
    check(!chosen.empty() && label_text(n.dropdown_label).rfind("已选择：", 0) == 0,
          "Down + Enter chooses a city and the label shows it");

    const dg::PixelRect tip = bounds(n.tooltip_buttons[0]);
    feed(drive_.move(tip.x + 10, tip.y + 10));
    wait_for([&] { return w_.node_count() > baseline; }, 1000);
    check(w_.node_count() > baseline, "hovering 保存 shows its tooltip");
    feed(drive_.move(5, 5));
    wait_for([&] { return w_.node_count() == baseline; }, 1000);

    for (int round = 0; round < 5; ++round) {
      right_click();
      key(dg::Key::kEscape);
      click(n.info_button);
      key(dg::Key::kEscape);
      click(n.dropdown);
      key(dg::Key::kEscape);
    }
    feed(drive_.move(5, 5));
    check(w_.node_count() == baseline,
          "after 5 rounds of menu/dialog/dropdown the node count is back at " +
              std::to_string(baseline) + " (now " + std::to_string(w_.node_count()) + ")");
    (void)app_.set_tooltip_delay(400);
  }

  void images() {
    out_ << "\n图片: PNG / JPEG / WebP decoded, SVG icons tinted\n";
    tab(kImages);
    const Nodes& n = demo_.nodes();
    bool decoded = true;
    for (const dg::ImageId id : n.bitmap_ids) {
      const std::optional<dg::PixelSize> size = app_.images().pixel_size(id);
      decoded = decoded && id.is_valid() && size.has_value() && size->width == 240 &&
                size->height == 150;
    }
    check(decoded, "all three formats decode to 240x150");
    const dg::RasterSurface* frame = w_.frame();
    const dg::PixelRect jpeg = bounds(n.bitmaps[1]);
    bool painted = frame != nullptr;
    if (painted) {
      const dg::PixelView view = frame->peek_pixels();
      const std::size_t at = (static_cast<std::size_t>(jpeg.y + 20) * view.row_bytes) +
                             (static_cast<std::size_t>(jpeg.x + 20) * 4);
      // Sky blue: more blue than red.
      painted = view.pixels[at] > view.pixels[at + 2] + 40;
    }
    check(painted, "the JPEG is painted (sky-blue pixel inside it)");
  }

  void window() {
    out_ << "\n窗口: title, timer ticks, waker\n";
    tab(kWindow);
    const Nodes& n = demo_.nodes();
    click(n.title_button);
    check(w_.title().value_or("") == "标题改好了 ✓" &&
              label_text(n.log_lines[0]).find("标题改为") == 0,
          "改标题 sets the OS title and logs it");
    check(wait_for([&] { return demo_.ticks() >= 1; }, 2500) &&
              label_text(n.tick_label) != "已运行 0 秒",
          "the 1 s timer ticks the counter");
    click(n.waker_button);
    check(wait_for([&] { return demo_.woken_payload() == kWakerPayload; }, 2000) &&
              label_text(n.waker_label).find("payload = 42") != std::string::npos,
          "a background thread's Waker::post_user_event() arrives as a UserEvent");
    click(n.maximize_button);
    click(n.restore_button);
    check(!demo_.status_text().empty(), "maximize/restore requests go through without error");
  }

  dg::App& app_;
  Showcase& demo_;
  dg::Window& w_;
  example::Driver drive_;
  std::ostream& out_;
  bool ok_ = true;
};

}  // namespace

int verify(dg::App& app, std::ostream& out) {
  dg::Window* window = open_window(app);
  if (window == nullptr) {
    return 1;
  }
  Showcase demo(app, *window, make_theme(), dg::ThemeVariant::kLight);
  return Check(app, demo, out).run();
}

}  // namespace showcase
