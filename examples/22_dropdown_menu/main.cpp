// examples/22_dropdown_menu: a dropdown between two buttons, on dg::App /
// dg::Window.
//
// The runtime opens the option list as an overlay popup, moves the
// highlight with Up/Down, commits with Enter or a click, and reports every
// commit as a ValueChangedEvent; this file only reacts to it.
//
//   (no arguments)            the demo window
//   --branch overlay          accepted for symmetry; only "overlay" exists
//   --run-ms N                close after N milliseconds
//   --font-dir DIR            scan a different font directory
//   --verify-dropdown         the headless check. No display needed.
//   --dump-png FILE           one frame (third option selected) to PNG

#include <algorithm>
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

using dg::NodeId;
using dg::NodeKind;

const std::vector<std::string> kOptions = {"Apple", "Banana", "Cherry", "Date", "Elderberry"};
constexpr int kWidth = 520;
constexpr int kHeight = 200;

enum class Mode : std::uint8_t { kWindow, kVerify, kDumpPng };

struct Options {
  Mode mode = Mode::kWindow;
  int run_ms = 0;
  std::string font_dir = "/usr/share/fonts";
  std::string png_path;
};

struct Scene {
  dg::Window* window = nullptr;
  NodeId before;
  NodeId dropdown;
  NodeId after;
};

NodeId button(dg::Window& w, NodeId parent, const std::string& text) {
  const NodeId node = w.create(NodeKind::kButton, parent).value();
  (void)w.set(node, {dg::prop::width(90), dg::prop::height(32)});
  (void)w.set_text(node, DG_PROP_TEXT, text);
  return node;
}

Scene open(dg::App& app) {
  dg::WindowOptions options;
  options.title = "drawgui dropdown";
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
                     dg::prop::gap(16)});
  for (const std::uint16_t id :
       {DG_PROP_PADDING_L, DG_PROP_PADDING_T, DG_PROP_PADDING_R, DG_PROP_PADDING_B}) {
    (void)w.set_prop(body, id, dg::PropValue::number(20));
  }
  Scene s{&w, {}, {}, {}};
  s.before = button(w, body, "Before");
  s.dropdown = w.create(NodeKind::kDropdown, body).value();
  (void)w.set(s.dropdown, {dg::prop::width(180), dg::prop::height(32)});
  (void)w.set_text(s.dropdown, DG_PROP_TEXT, "Choose a fruit");
  (void)w.dropdown(s.dropdown).value().set_items(kOptions);
  s.after = button(w, body, "After");
  return s;
}

// An open option row is a node whose LAST child carries the option text
// (and, for the selected option, a check-mark child before it).
std::string row_text(const dg::RenderTree& tree, std::optional<NodeId> row) {
  if (!row.has_value() || !tree.is_valid(*row) || tree.children(*row).empty()) {
    return {};
  }
  return tree.style(tree.children(*row).back()).text.text;
}

// The open list's rows that carry a check mark, by option text.
std::vector<std::string> checked_rows(const dg::RenderTree& tree) {
  std::vector<std::string> out;
  for (const NodeId node : dg::subtree_of(tree, dg::RenderTree::root())) {
    const std::vector<NodeId> kids = tree.children(node);
    if (kids.size() == 2 && !tree.style(kids.front()).text.text.empty() &&
        std::find(kOptions.begin(), kOptions.end(), tree.style(kids.back()).text.text) !=
            kOptions.end()) {
      out.push_back(tree.style(kids.back()).text.text);
    }
  }
  return out;
}

int verify(dg::App& app, std::ostream& out) {
  const Scene s = open(app);
  if (s.window == nullptr) {
    return 1;
  }
  dg::Window& w = *s.window;
  example::Driver drive(app, w);
  drive.pump();
  bool ok = true;
  auto check = [&](bool pass, const std::string& what) { example::check(pass, what, out, ok); };
  const dg::RenderTree& tree = w.layout().render();
  dg::DropdownRef dropdown = w.dropdown(s.dropdown).value();
  auto highlighted = [&] { return row_text(tree, w.focused()); };
  auto changes = [](const std::vector<dg::Event>& events) {
    return example::events_of<dg::events::ValueChangedEvent>(events);
  };

  out << "Tab order: before -> dropdown -> after\n";
  std::vector<NodeId> order;
  for (int i = 0; i < 3; ++i) {
    drive.key(dg::Key::kTab);
    order.push_back(w.focused().value_or(NodeId{}));
  }
  check(order == std::vector<NodeId>{s.before, s.dropdown, s.after},
        "Tab visits [before, dropdown, after]");

  out << "\noverlay popup: keyboard nav at first/middle/last + wraparound, mouse\n";
  const int baseline = w.node_count();
  (void)w.focus(s.dropdown);
  drive.key(dg::Key::kDown);
  check(highlighted() == "Apple",
        "Down on the focused anchor opens the list, highlight on option 0 (\"Apple\")");
  std::vector<dg::events::ValueChangedEvent> changed = changes(drive.key(dg::Key::kEnter));
  check(changed.size() == 1 && changed[0].selected == 0 && dropdown.selected().value() == 0,
        "Enter at the FIRST option (\"Apple\", index 0) commits it");
  check(w.focused() == s.dropdown, "closing returns focus to the anchor");

  drive.key(dg::Key::kEnter);
  check(highlighted() == "Apple",
        "reopening seeds the highlight at the just-committed selection (index 0)");
  drive.key(dg::Key::kDown);
  drive.key(dg::Key::kDown);
  check(highlighted() == "Cherry", "Down x2 from index 0 highlights index 2 (\"Cherry\")");
  changed = changes(drive.key(dg::Key::kEnter));
  check(changed.size() == 1 && changed[0].previous == 0 && changed[0].selected == 2,
        "Enter at a MIDDLE option (\"Cherry\", index 2) commits it and reports the previous "
        "index");

  drive.key(dg::Key::kDown);
  check(highlighted() == "Cherry", "reopening seeds the highlight at index 2 again");
  drive.key(dg::Key::kDown);
  drive.key(dg::Key::kDown);
  check(highlighted() == "Elderberry", "Down x2 from index 2 highlights index 4 (last)");
  drive.key(dg::Key::kDown);
  check(highlighted() == "Apple", "Down from the LAST option wraps FORWARD to index 0");
  drive.key(dg::Key::kUp);
  check(highlighted() == "Elderberry", "Up from the FIRST option wraps BACKWARD to the last");
  drive.key(dg::Key::kEnter);
  check(dropdown.selected().value() == 4,
        "Enter at the LAST option (\"Elderberry\", index 4) commits it");

  drive.key(dg::Key::kDown);
  drive.key(dg::Key::kDown);
  check(highlighted() == "Apple", "Down from index 4 wraps to index 0 (highlight only)");
  changed = changes(drive.key(dg::Key::kEscape));
  check(changed.empty() && dropdown.selected().value() == 4 && w.focused() == s.dropdown,
        "closing WITHOUT Enter (Escape) leaves the prior selection (index 4) untouched");

  drive.key(dg::Key::kDown);
  changed = changes(drive.click(kWidth - 2, kHeight - 2));
  check(changed.empty() && dropdown.selected().value() == 4 && w.node_count() == baseline,
        "a click outside closes the list without changing the selection");

  (void)w.focus(s.dropdown);
  drive.key(dg::Key::kDown);
  check(highlighted() == "Elderberry", "reopening seeds the highlight at index 4 again");

  out << "\nselected row: the open list checks the current selection and no other option\n";
  check(checked_rows(tree) == std::vector<std::string>{"Elderberry"},
        "exactly the selected option carries the check mark");

  std::optional<NodeId> banana;
  for (const NodeId node : dg::subtree_of(tree, dg::RenderTree::root())) {
    if (row_text(tree, node) == "Banana") {
      banana = node;
    }
  }
  changed = banana.has_value() ? changes(drive.click(*banana)) : std::vector<dg::events::ValueChangedEvent>{};
  check(changed.size() == 1 && changed[0].selected == 1,
        "a click on row 1 (\"Banana\") commits it even though index 4 was highlighted");
  check(w.node_count() == baseline,
        "after every close the live node count is back at its baseline");

  out << "\n" << (ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED") << "\n";
  return ok ? 0 : 1;
}

int dump_png(dg::App& app, const std::string& path, std::ostream& out) {
  const Scene s = open(app);
  if (s.window == nullptr) {
    return 1;
  }
  (void)s.window->dropdown(s.dropdown).value().select(2);
  (void)s.window->focus(s.dropdown);
  example::Driver(app, *s.window).pump();
  return example::write_png(*s.window, path, out);
}

int run_window(dg::App& app, int run_ms) {
  const Scene s = open(app);
  if (s.window == nullptr) {
    return 1;
  }
  example::close_after(app, run_ms);
  app.run([&](const dg::Event& event) {
    if (dg::event_as<dg::events::TimerEvent>(event) != nullptr) {
      app.quit();
    } else if (const auto* changed = dg::event_as<dg::events::ValueChangedEvent>(event)) {
      std::cout << "selected " << kOptions[static_cast<std::size_t>(changed->selected)] << "\n";
    } else if (const auto* click = dg::event_as<dg::events::ClickEvent>(event)) {
      std::cout << (click->node == s.before ? "Before" : "After") << " clicked\n";
    }
  });
  return 0;
}

std::optional<Options> parse(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    const bool has_value = i + 1 < args.size();
    if (arg == "--verify-dropdown") {
      options.mode = Mode::kVerify;
    } else if (arg == "--dump-png" && has_value) {
      options.mode = Mode::kDumpPng;
      options.png_path = args[++i];
    } else if (arg == "--run-ms" && has_value) {
      options.run_ms = example::parse_int(args[++i]).value_or(0);
    } else if (arg == "--font-dir" && has_value) {
      options.font_dir = args[++i];
    } else if (arg == "--branch" && has_value) {
      if (args[++i] != "overlay") {
        std::cerr << "--branch " << args[i] << " is not supported: only overlay popups exist\n";
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
    std::cerr << "usage: drawgui_dropdown_menu [--branch overlay] [--run-ms N]\n"
              << "                             [--font-dir DIR] [--dump-png FILE]\n"
              << "                             [--verify-dropdown]\n";
    return 2;
  }
  if (options->mode != Mode::kWindow) {
    example::use_headless_driver();
  }
  dg::Expected<dg::App, dg::AppError> app =
      example::make_app(options->font_dir, "DejaVu Sans", std::cout);
  if (!app) {
    return 1;
  }
  switch (options->mode) {
    case Mode::kVerify:
      return verify(app.value(), std::cout);
    case Mode::kDumpPng:
      return dump_png(app.value(), options->png_path, std::cout);
    case Mode::kWindow:
      break;
  }
  return run_window(app.value(), options->run_ms);
}
