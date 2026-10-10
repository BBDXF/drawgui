// examples/29_enterprise: 资产管理 - an asset inventory desktop app on the
// public runtime dg::App / dg::Window. A menu bar and shortcuts sharing one
// command switch, a toolbar with SVG icon buttons, a location tree that
// filters a sortable table, a detail panel (常规 / 历史 / 标签), form dialogs
// and a status bar.
//
//   (no arguments)                         the app window
//   --theme light|dark                     the starting theme (default light)
//   --run-ms N                             close after N milliseconds
//   --font-dir DIR                         where the UI fonts are looked for
//   --verify                               the headless check (CTest). No display needed.
//   --dump-png FILE [--state S]            one frame to PNG. No display needed.
//        S = default | menu | context | dialog

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "drawgui/app/app.h"
#include "drawgui/app/window.h"
#include "example_app.h"
#include "inventory.h"
#include "probe.h"

namespace {

enum class Mode : std::uint8_t { kWindow, kVerify, kDumpPng };

struct Options {
  Mode mode = Mode::kWindow;
  std::string font_dir = "/usr/share/fonts";
  std::string png_path;
  std::string state = "default";
  int run_ms = 0;
  dg::ThemeVariant variant = dg::ThemeVariant::kLight;
};

std::optional<Options> parse(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    const bool has_value = i + 1 < args.size();
    if (arg == "--verify") {
      options.mode = Mode::kVerify;
    } else if (arg == "--dump-png" && has_value) {
      options.mode = Mode::kDumpPng;
      options.png_path = args[++i];
    } else if (arg == "--state" && has_value) {
      options.state = args[++i];
      if (options.state != "default" && options.state != "menu" && options.state != "context" &&
          options.state != "dialog") {
        return std::nullopt;
      }
    } else if (arg == "--theme" && has_value) {
      const std::string& name = args[++i];
      if (name != "light" && name != "dark") {
        return std::nullopt;
      }
      options.variant = name == "dark" ? dg::ThemeVariant::kDark : dg::ThemeVariant::kLight;
    } else if (arg == "--run-ms" && has_value) {
      options.run_ms = example::parse_int(args[++i]).value_or(0);
    } else if (arg == "--font-dir" && has_value) {
      options.font_dir = args[++i];
    } else {
      std::cerr << "unrecognized argument: " << arg << "\n";
      return std::nullopt;
    }
  }
  return options;
}

int dump_png(dg::App& app, const Options& options) {
  dg::Window* window = inventory::open_window(app);
  if (window == nullptr) {
    return 1;
  }
  inventory::Inventory inv(app, *window, options.variant);
  inventory::Probe probe(app, *window, inv);
  probe.pump(3);
  // A selected row so the detail panel has something to show.
  probe.click_row(1);
  probe.click_text("历史", inv.nodes().tabs);
  if (options.state == "menu") {
    probe.chord(dg::LogicalKey::kF10);
    probe.key(dg::Key::kRight);
    probe.key(dg::Key::kDown);
  } else if (options.state == "context") {
    probe.click_row(inv.visible()[3], dg::PointerButton::kSecondary);
  } else if (options.state == "dialog") {
    probe.click_text("常规", inv.nodes().tabs);
    inv.run(inventory::kCmdEdit);
  }
  probe.pump(4);
  return example::write_png(*window, options.png_path, std::cout);
}

int run_window(dg::App& app, const Options& options) {
  dg::Window* window = inventory::open_window(app);
  if (window == nullptr) {
    return 1;
  }
  inventory::Inventory inv(app, *window, options.variant);
  const dg::TimerId quit_timer =
      options.run_ms > 0 ? app.add_timer(options.run_ms, false) : dg::TimerId{};
  app.run([&](const dg::Event& event) {
    if (const auto* timer = dg::event_as<dg::events::TimerEvent>(event)) {
      if (options.run_ms > 0 && timer->timer == quit_timer) {
        app.quit();
        return;
      }
    }
    inv.on_event(event);
  });
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<Options> options = parse(argc, argv);
  if (!options.has_value()) {
    std::cerr
        << "usage: drawgui_enterprise [--theme light|dark] [--run-ms N] [--font-dir DIR]\n"
           "                          [--dump-png FILE [--state default|menu|context|dialog]]\n"
           "                          [--verify]\n";
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
  (void)app.value().add_font(options->font_dir, "WenQuanYi Micro Hei");
  switch (options->mode) {
    case Mode::kVerify:
      return inventory::verify(app.value(), std::cout);
    case Mode::kDumpPng:
      return dump_png(app.value(), *options);
    case Mode::kWindow:
      break;
  }
  return run_window(app.value(), *options);
}
