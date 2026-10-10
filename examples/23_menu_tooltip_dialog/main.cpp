// examples/23_menu_tooltip_dialog: a context menu, a hover tooltip and a
// modal dialog on dg::App / dg::Window.
//
// The runtime does the popup work: open_menu() opens the menu as an
// overlay and reports the choice as a MenuEvent; set_tooltip() binds a
// tooltip that the runtime times, shows and hides by itself; open_dialog()
// opens a modal dialog that keeps focus inside it and reports the button as
// a DialogResultEvent. This file only reacts to the events.
//
//   (no arguments)              the demo window
//   --branch overlay            accepted for symmetry; dialogs are overlays
//   --run-ms N                  close after N milliseconds
//   --font-dir DIR              scan a different font directory
//   --tooltip-delay-ms N        hover delay before a tooltip shows (default 400)
//   --verify-menus              the headless check. No display needed.
//   --idle-probe-ms N           measure idle CPU with a tooltip bound but nothing hovered
//   --dump-png FILE             one frame to PNG. No display needed.

#include <sys/resource.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "drawgui/app/app.h"
#include "drawgui/app/prop.h"
#include "drawgui/app/window.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/render/prop_ids.generated.h"
#include "example_app.h"

namespace {

using Clock = std::chrono::steady_clock;
using dg::NodeId;
using dg::NodeKind;

const std::vector<std::string> kMenuItems = {"Copy", "Paste", "Delete"};
constexpr int kWidth = 620;
constexpr int kHeight = 220;

enum class Mode : std::uint8_t { kWindow, kVerify, kDumpPng, kIdleProbe };

struct Options {
  Mode mode = Mode::kWindow;
  int run_ms = 0;
  int tooltip_delay_ms = 400;
  int idle_probe_ms = 0;
  std::string font_dir = "/usr/share/fonts";
  std::string png_path;
};

struct Scene {
  dg::Window* window = nullptr;
  NodeId before;
  NodeId menu_target;
  NodeId hover_target;
  NodeId open_dialog;
  NodeId after;
};

NodeId button(dg::Window& w, NodeId parent, float width, const std::string& text) {
  const NodeId node = w.create(NodeKind::kButton, parent).value();
  (void)w.set(node, {dg::prop::width(width), dg::prop::height(32)});
  (void)w.set_text(node, DG_PROP_TEXT, text);
  return node;
}

Scene open(dg::App& app) {
  dg::WindowOptions options;
  options.title = "drawgui menu/tooltip/dialog";
  options.width = kWidth;
  options.height = kHeight;
  options.background = dg::Color::from_argb(0xFF0E1218);
  dg::Expected<dg::Window*, dg::AppError> made = app.create_window(options);
  if (!made) {
    std::cerr << "create_window() failed: " << made.error().message << "\n";
    return {};
  }
  dg::Window& w = *made.value();
  const NodeId body = w.create(NodeKind::kBox).value();
  (void)w.set(body, {dg::prop::width(kWidth), dg::prop::height(kHeight), dg::prop::row(),
                     dg::prop::gap(12)});
  for (const std::uint16_t id :
       {DG_PROP_PADDING_L, DG_PROP_PADDING_T, DG_PROP_PADDING_R, DG_PROP_PADDING_B}) {
    (void)w.set_prop(body, id, dg::PropValue::number(20));
  }
  Scene s{&w, {}, {}, {}, {}, {}};
  s.before = button(w, body, 80, "Before");
  s.menu_target = button(w, body, 150, "Right-click me");
  s.hover_target = button(w, body, 120, "Hover me");
  s.open_dialog = button(w, body, 120, "Open Dialog");
  s.after = button(w, body, 80, "After");
  (void)w.set_tooltip(s.hover_target, "This button opens a tooltip");
  return s;
}

std::vector<dg::MenuEntry> menu_entries() {
  std::vector<dg::MenuEntry> entries;
  entries.reserve(kMenuItems.size());
  for (const std::string& text : kMenuItems) {
    entries.push_back(dg::MenuEntry{text, false, false, std::nullopt});
  }
  return entries;
}

dg::DialogOptions dialog_options() {
  dg::DialogOptions options;
  options.title = "Modal dialog - Tab/click cannot escape";
  options.buttons = {"Close"};
  options.cancel_button = 0;
  return options;
}

// What the demo does with each event; returns the menu/dialog id it opened.
void react(dg::Window& w, const Scene& s, const dg::Event& event, std::ostream& out) {
  if (const auto* context = dg::event_as<dg::events::ContextClickEvent>(event)) {
    if (context->node == s.menu_target) {
      (void)w.open_menu(std::nullopt, dg::PixelPoint{context->x, context->y}, menu_entries());
    }
  } else if (const auto* click = dg::event_as<dg::events::ClickEvent>(event)) {
    if (click->node == s.open_dialog && !w.has_dialog()) {
      (void)w.open_dialog(dialog_options());
    }
  } else if (const auto* menu = dg::event_as<dg::events::MenuEvent>(event)) {
    out << (menu->item.has_value() ? "menu: " + kMenuItems[static_cast<std::size_t>(*menu->item)]
                                   : std::string("menu dismissed"))
        << "\n";
  } else if (const auto* result = dg::event_as<dg::events::DialogResultEvent>(event)) {
    out << "dialog closed with button " << result->button << "\n";
  }
}

// The text of the menu row `node` (its last child carries the label).
std::string row_text(const dg::RenderTree& tree, std::optional<NodeId> node) {
  if (!node.has_value() || !tree.is_valid(*node) || tree.children(*node).empty()) {
    return {};
  }
  return tree.style(tree.children(*node).back()).text.text;
}

std::optional<NodeId> row_named(const dg::RenderTree& tree, const std::string& text) {
  for (const NodeId node : dg::subtree_of(tree, dg::RenderTree::root())) {
    if (row_text(tree, node) == text) {
      return node;
    }
  }
  return std::nullopt;
}

int verify(dg::App& app, std::ostream& out) {
  const Scene s = open(app);
  if (s.window == nullptr) {
    return 1;
  }
  dg::Window& w = *s.window;
  (void)app.set_tooltip_delay(400);
  example::Driver drive(app, w);
  drive.pump();
  bool ok = true;
  auto check = [&](bool pass, const std::string& what) { example::check(pass, what, out, ok); };
  const dg::RenderTree& tree = w.layout().render();
  auto feed = [&](const std::vector<dg::Event>& events) {
    for (const dg::Event& e : events) {
      react(w, s, e, out);
    }
    drive.pump(1);
    return events;
  };
  const int baseline = w.node_count();

  out << "button identity: secondary, primary and middle presses\n";
  const dg::PixelRect target = drive.bounds(s.menu_target);
  const dg::PixelPoint at{target.x + 12, target.y + 8};
  dg::WindowManager& wm = app.window_manager();
  wm.warp_pointer(w.id(), at.x, at.y);
  wm.post_pointer_button(w.id(), true, at.x, at.y, dg::PointerButton::kMiddle);
  wm.post_pointer_button(w.id(), false, at.x, at.y, dg::PointerButton::kMiddle);
  const std::vector<dg::Event> middle = drive.pump();
  check(example::events_of<dg::events::ClickEvent>(middle).empty() &&
            example::events_of<dg::events::ContextClickEvent>(middle).empty(),
        "a middle press is neither a click nor a context click");
  wm.post_pointer_button(w.id(), true, at.x, at.y, dg::PointerButton::kSecondary);
  wm.post_pointer_button(w.id(), false, at.x, at.y, dg::PointerButton::kSecondary);
  const std::vector<dg::Event> secondary = drive.pump();
  const std::vector<dg::events::ContextClickEvent> context =
      example::events_of<dg::events::ContextClickEvent>(secondary);
  check(context.size() == 1 && context[0].node == s.menu_target &&
            example::events_of<dg::events::ClickEvent>(secondary).empty(),
        "a secondary press is a ContextClickEvent on menu_target, never a ClickEvent");
  check(!w.focused().has_value(), "...and it does not move focus (a non-activating channel)");

  out << "\ncontext menu: opened at the pointer, Up/Down/Enter, Escape, click-outside\n";
  feed(secondary);
  const std::optional<NodeId> copy = row_named(tree, "Copy");
  check(copy.has_value() && drive.bounds(tree.parent(*copy)).x == at.x &&
            drive.bounds(tree.parent(*copy)).y == at.y,
        "the menu opens anchored at the POINTER position, not menu_target's own bounds");
  check(row_text(tree, w.focused_node()) == "Copy", "opening seeds the highlight at the first item");
  drive.key(dg::Key::kDown);
  check(row_text(tree, w.focused_node()) == "Paste", "Down moves the highlight to the second item");
  drive.key(dg::Key::kDown);
  check(row_text(tree, w.focused_node()) == "Delete", "Down again reaches the LAST item");
  drive.key(dg::Key::kDown);
  check(row_text(tree, w.focused_node()) == "Copy", "Down from the last item WRAPS to the first");
  drive.key(dg::Key::kUp);
  check(row_text(tree, w.focused_node()) == "Delete", "Up from the first item WRAPS to the last");
  std::vector<dg::events::MenuEvent> menu =
      example::events_of<dg::events::MenuEvent>(drive.key(dg::Key::kEscape));
  check(menu.size() == 1 && !menu[0].item.has_value() && w.node_count() == baseline,
        "Escape closes the menu without a selection");

  (void)w.open_menu(std::nullopt, at, menu_entries());
  drive.pump();
  menu = example::events_of<dg::events::MenuEvent>(drive.click(kWidth - 4, kHeight - 4));
  check(menu.size() == 1 && !menu[0].item.has_value(),
        "a click OUTSIDE the menu's bounds closes it without a selection");

  (void)w.open_menu(std::nullopt, at, menu_entries());
  drive.pump();
  drive.key(dg::Key::kDown);
  menu = example::events_of<dg::events::MenuEvent>(drive.key(dg::Key::kEnter));
  check(menu.size() == 1 && menu[0].item == 1, "Enter activates the highlighted item (\"Paste\")");
  check(w.node_count() == baseline,
        "every close destroyed the menu's nodes - the live node count is back at baseline");

  out << "\ntooltip: shown by the runtime after the hover delay\n";
  const dg::PixelRect hover = drive.bounds(s.hover_target);
  const Clock::time_point hovered_at = Clock::now();
  drive.move(hover.x + 10, hover.y + 10);
  check(w.node_count() == baseline, "before the delay, nothing is shown");
  while (w.node_count() == baseline && Clock::now() - hovered_at < std::chrono::seconds(3)) {
    (void)app.pump(-1);
  }
  const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() -
                                                                            hovered_at);
  check(w.node_count() > baseline && waited.count() >= 400,
        "the bubble opens once the 400 ms delay has passed (" + std::to_string(waited.count()) +
            " ms)");
  drive.press(hover.x + 10, hover.y + 10, true);
  drive.press(hover.x + 10, hover.y + 10, false);
  check(w.node_count() == baseline, "a press hides it, destroying its nodes");
  const Clock::time_point pressed_at = Clock::now();
  while (Clock::now() - pressed_at < std::chrono::milliseconds(700)) {
    (void)app.pump(50);
  }
  check(w.node_count() == baseline, "...and it stays hidden while the pointer is still over it");
  drive.move(kWidth - 4, kHeight - 4);

  out << "\ndialog: modal focus refusal\n";
  feed(drive.click(s.open_dialog));
  check(w.has_dialog(), "clicking \"Open Dialog\" opens the dialog");
  const std::optional<NodeId> close = w.focused_node();
  check(close.has_value() && !w.kind_of(*close).has_value(),
        "the dialog's own Close button is focused on open (inside the dialog)");
  drive.key(dg::Key::kTab);
  drive.key(dg::Key::kTab, true);
  check(w.focused_node() == close, "Tab and Shift+Tab never leave the dialog");
  const std::vector<dg::Event> outside = feed(drive.click(s.after));
  // Focus stays where it was; it never reaches `after`.
  check(example::events_of<dg::events::ClickEvent>(outside).empty() && w.has_dialog() &&
            w.focused_node() != s.after,
        "a click outside the dialog reaches nothing behind it and cannot move focus there");
  const std::vector<dg::events::DialogResultEvent> result =
      example::events_of<dg::events::DialogResultEvent>(drive.key(dg::Key::kEscape));
  check(result.size() == 1 && result[0].button == 0 && !w.has_dialog(),
        "Escape closes the dialog, reporting its cancel button");
  check(w.node_count() == baseline, "closing it destroyed its nodes");

  out << "\n" << (ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED") << "\n";
  return ok ? 0 : 1;
}

int dump_png(dg::App& app, const std::string& path, std::ostream& out) {
  const Scene s = open(app);
  if (s.window == nullptr) {
    return 1;
  }
  (void)s.window->focus(s.menu_target);
  example::Driver(app, *s.window).pump();
  return example::write_png(*s.window, path, out);
}

double cpu_ms() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  const auto ms = [](const timeval& tv) {
    return (static_cast<double>(tv.tv_sec) * 1000.0) + (static_cast<double>(tv.tv_usec) / 1000.0);
  };
  return ms(usage.ru_utime) + ms(usage.ru_stime);
}

// pump(-1) blocks while nothing is pending, tooltip bound or not.
int idle_probe(dg::App& app, int probe_ms, std::ostream& out) {
  if (open(app).window == nullptr) {
    return 1;
  }
  (void)app.pump(200);
  const dg::TimerId stop = app.add_timer(probe_ms, false);
  const double cpu_before = cpu_ms();
  const Clock::time_point start = Clock::now();
  int wakeups = 0;
  bool done = false;
  while (!done) {
    for (const dg::Event& e : app.pump(-1)) {
      const auto* timer = dg::event_as<dg::events::TimerEvent>(e);
      done = done || (timer != nullptr && timer->timer == stop);
    }
    ++wakeups;
  }
  const double wall = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  const double cpu = cpu_ms() - cpu_before;
  out << "idle probe with a tooltip bound but NOTHING hovered:\n"
      << "  requested block:  " << probe_ms << " ms\n"
      << "  actual wall time: " << wall << " ms across " << wakeups << " pump() call(s)\n"
      << "  process CPU time consumed (user+sys) while blocked: " << cpu << " ms\n"
      << "  CPU utilisation over the block: " << (wall > 0.0 ? cpu / wall * 100.0 : 0.0)
      << "%\n";
  return 0;
}

int run_window(dg::App& app, int run_ms) {
  const Scene s = open(app);
  if (s.window == nullptr) {
    return 1;
  }
  std::cout << "menu/tooltip/dialog demo\n"
            << "  Right-click \"Right-click me\" for a context menu; hover \"Hover me\" for a "
               "tooltip; \"Open Dialog\" opens a modal dialog.\n";
  example::close_after(app, run_ms);
  app.run([&](const dg::Event& event) {
    if (dg::event_as<dg::events::TimerEvent>(event) != nullptr) {
      app.quit();
    }
    react(*s.window, s, event, std::cout);
  });
  return 0;
}

std::optional<Options> parse(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (arg == "--verify-menus") {
      options.mode = Mode::kVerify;
      continue;
    }
    if (i + 1 >= args.size()) {
      return std::nullopt;
    }
    const std::string& value = args[++i];
    if (arg == "--dump-png") {
      options.mode = Mode::kDumpPng;
      options.png_path = value;
    } else if (arg == "--font-dir") {
      options.font_dir = value;
    } else if (arg == "--run-ms") {
      options.run_ms = example::parse_int(value).value_or(0);
    } else if (arg == "--tooltip-delay-ms") {
      options.tooltip_delay_ms = example::parse_int(value).value_or(0);
    } else if (arg == "--idle-probe-ms") {
      options.mode = Mode::kIdleProbe;
      options.idle_probe_ms = example::parse_int(value).value_or(0);
    } else if (arg == "--branch") {
      if (value != "overlay") {
        std::cerr << "--branch " << value
                  << " is not supported: the runtime's dialogs are overlays\n";
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<Options> options = parse(argc, argv);
  if (!options.has_value()) {
    std::cerr << "usage: drawgui_menu_tooltip_dialog [--branch overlay] [--run-ms N]\n"
              << "         [--font-dir DIR] [--tooltip-delay-ms N] [--verify-menus]\n"
              << "         [--idle-probe-ms N] [--dump-png FILE]\n";
    return 2;
  }
  if (options->mode == Mode::kVerify || options->mode == Mode::kDumpPng) {
    example::use_headless_driver();
  }
  dg::Expected<dg::App, dg::AppError> app =
      example::make_app(options->font_dir, "DejaVu Sans", std::cout);
  if (!app) {
    return 1;
  }
  (void)app.value().set_tooltip_delay(options->tooltip_delay_ms);
  switch (options->mode) {
    case Mode::kVerify:
      return verify(app.value(), std::cout);
    case Mode::kDumpPng:
      return dump_png(app.value(), options->png_path, std::cout);
    case Mode::kIdleProbe:
      return idle_probe(app.value(), options->idle_probe_ms, std::cout);
    case Mode::kWindow:
      break;
  }
  return run_window(app.value(), options->run_ms);
}
