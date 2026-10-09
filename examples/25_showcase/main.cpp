// examples/25_showcase: the one demo of every drawgui feature, on the public
// runtime dg::App / dg::Window. Seven tabs (基础控件, 文本, 布局, 数据, 弹层,
// 图片, 窗口), a header with a live light/dark switch and a status line that
// names the last event.
//
//   (no arguments)              the demo window
//   --theme light|dark          the starting theme (default light)
//   --tab N                     the starting tab, 0..6
//   --run-ms N                  close after N milliseconds
//   --font-dir DIR              where the UI fonts are looked for
//   --verify-showcase           the headless check (CTest). No display needed.
//   --dump-png FILE             one frame of --tab/--theme to PNG. No display needed.

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "drawgui/app/app.h"
#include "drawgui/app/window.h"
#include "example_app.h"
#include "showcase.h"
#include "showcase_check.h"

namespace {

enum class Mode : std::uint8_t { kWindow, kVerify, kDumpPng };

struct Options {
  Mode mode = Mode::kWindow;
  std::string font_dir = "/usr/share/fonts";
  std::string png_path;
  int tab = 0;
  int run_ms = 0;
  dg::ThemeVariant variant = dg::ThemeVariant::kLight;
};

std::optional<Options> parse(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    const bool has_value = i + 1 < args.size();
    if (arg == "--verify-showcase") {
      options.mode = Mode::kVerify;
    } else if (arg == "--dump-png" && has_value) {
      options.mode = Mode::kDumpPng;
      options.png_path = args[++i];
    } else if (arg == "--tab" && has_value) {
      const std::optional<int> tab = example::parse_int(args[++i]);
      if (!tab.has_value() || *tab < 0 || *tab >= showcase::kTabCount) {
        return std::nullopt;
      }
      options.tab = *tab;
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
  dg::Window* window = showcase::open_window(app);
  if (window == nullptr) {
    return 1;
  }
  showcase::Showcase demo(app, *window, showcase::make_theme(), options.variant);
  demo.select_tab(options.tab);
  // A little life in the data page: a selection and a half-full bar.
  (void)window->list(demo.nodes().list).value().select(2);
  (void)window->tree(demo.nodes().tree).value().select(1);
  demo.set_progress(0.62F);
  example::Driver(app, *window).pump(4);
  return example::write_png(*window, options.png_path, std::cout);
}

int run_window(dg::App& app, const Options& options) {
  dg::Window* window = showcase::open_window(app);
  if (window == nullptr) {
    return 1;
  }
  showcase::Showcase demo(app, *window, showcase::make_theme(), options.variant);
  demo.select_tab(options.tab);
  demo.start_timers();
  const dg::TimerId quit_timer =
      options.run_ms > 0 ? app.add_timer(options.run_ms, false) : dg::TimerId{};
  app.run([&](const dg::Event& event) {
    if (const auto* timer = dg::event_as<dg::events::TimerEvent>(event)) {
      if (options.run_ms > 0 && timer->timer == quit_timer) {
        app.quit();
        return;
      }
    }
    demo.on_event(event);
  });
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<Options> options = parse(argc, argv);
  if (!options.has_value()) {
    std::cerr
        << "usage: drawgui_showcase [--theme light|dark] [--tab N] [--run-ms N]\n"
           "                        [--font-dir DIR] [--dump-png FILE] [--verify-showcase]\n";
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
  (void)app.value().add_font(options->font_dir, "Noto Color Emoji");
  switch (options->mode) {
    case Mode::kVerify:
      return showcase::verify(app.value(), std::cout);
    case Mode::kDumpPng:
      return dump_png(app.value(), *options);
    case Mode::kWindow:
      break;
  }
  return run_window(app.value(), *options);
}
