// examples/15_list: a virtualized 1000-item list on dg::App / dg::Window.
//
// NodeKind::kList keeps a fixed pool of row nodes and recycles them as the
// list scrolls; the runtime routes the wheel, the scrollbar and row clicks
// and repaints recycled rows itself. The low-level recipe (list_sync(),
// list_scroll_by(), sync_scrollbar()) is in doc/guide/widgets.md's
// "直接使用底层" section.
//
//   (no arguments)      the demo window - wheel, drag the scrollbar, click
//   --size WxH          open at a given size
//   --run-ms N          close after N milliseconds
//   --jump N            pre-scroll to item N (offscreen modes only)
//   --verify-list       the headless check. No display needed.
//   --dump-png FILE     one frame to PNG. No display needed.
//   --font-dir DIR      where "DejaVu Sans" is looked for

#include <charconv>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "drawgui/app/app.h"
#include "drawgui/app/prop.h"
#include "drawgui/app/window.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/widget/widget_set.h"
#include "example_app.h"

namespace {

using dg::NodeId;

constexpr int kItemCount = 1000;
constexpr int kItemHeight = 48;
constexpr int kViewportWidth = 340;
constexpr int kViewportHeight = 480;
// The runtime list draws a 1px frame (like TreeView / Table), so the rows
// scroll inside a viewport 2px shorter than the box.
constexpr int kListFrame = 1;
constexpr int kRowViewport = kViewportHeight - (2 * kListFrame);
constexpr int kWheelStepPx = 40;  // one wheel notch, as the runtime scrolls it

enum class Mode : std::uint8_t { kWindow, kVerify, kDumpPng };

struct Options {
  Mode mode = Mode::kWindow;
  dg::PixelSize size{460, 600};
  int run_ms = 0;
  int jump_items = 0;
  std::string png_path;
  std::string font_dir = "/usr/share/fonts";
};

std::string item_text(int index) {
  std::string digits = std::to_string(index);
  return "item #" + std::string(digits.size() < 4 ? 4 - digits.size() : 0, '0') + digits;
}

std::vector<std::string> items(int count) {
  std::vector<std::string> out;
  out.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    out.push_back(item_text(i));
  }
  return out;
}

struct Scene {
  dg::Window* window = nullptr;
  NodeId list;
};

Scene open(dg::App& app, dg::PixelSize size, int item_count) {
  dg::WindowOptions options;
  options.title = "drawgui list";
  options.width = size.width;
  options.height = size.height;
  options.background = dg::Color::from_argb(0xFF14171C);
  options.resizable = true;
  dg::Expected<dg::Window*, dg::AppError> made = app.create_window(options);
  if (!made) {
    std::cerr << "create_window() failed: " << made.error().message << "\n";
    return {};
  }
  dg::Window& w = *made.value();
  const NodeId root = w.create(dg::NodeKind::kBox).value();
  (void)w.set(root, {dg::prop::width_percent(100), dg::prop::height_percent(100),
                     dg::prop::column()});
  for (const std::uint16_t id : {DG_PROP_PADDING_L, DG_PROP_PADDING_T}) {
    (void)w.set_prop(root, id, dg::PropValue::number(40));
  }
  const NodeId list = w.create(dg::NodeKind::kList, root).value();
  (void)w.set(list, {dg::prop::width(kViewportWidth), dg::prop::height(kViewportHeight),
                     dg::prop::background(dg::Color::from_argb(0xFF1E232B))});
  (void)w.list(list).value().set_items(items(item_count), kItemHeight);
  return Scene{&w, list};
}

// Every pool row that shows an item must show exactly that item's text -
// read back from the row node, not from the list's own bookkeeping.
bool exact_content(const Scene& s, std::ostream& out, const char* moment) {
  const dg::Widget& widget = s.window->widgets().at(s.list);
  const dg::RenderTree& tree = s.window->layout().render();
  bool ok = true;
  for (std::size_t i = 0; i < widget.list_pool.size(); ++i) {
    const int logical = widget.list_assigned[i];
    if (logical < 0) {
      continue;
    }
    const std::string& shown = tree.style(widget.list_pool[i]).text.text;
    if (shown != item_text(logical)) {
      out << "  FAIL [" << moment << "]: row showing item " << logical << " reads \"" << shown
          << "\" (residue from a previous item)\n";
      ok = false;
    }
  }
  return ok;
}

int verify(dg::App& app, std::ostream& out) {
  out << "verify: virtualized list, on the scene the demo puts on screen\n";
  bool ok = true;
  auto check = [&](bool pass, const std::string& what) { example::check(pass, what, out, ok); };

  // Claim 1: the pool never grows with the data.
  Scene small = open(app, {460, 600}, 10);
  if (small.window == nullptr) {
    return 1;
  }
  example::Driver(app, *small.window).pump();
  const int small_nodes = small.window->node_count();
  app.destroy_window(small.window);
  Scene s = open(app, {460, 600}, kItemCount);
  if (s.window == nullptr) {
    return 1;
  }
  example::Driver drive(app, *s.window);
  drive.pump();
  const int nodes = s.window->node_count();
  check(small_nodes == nodes,
        "node_count is " + std::to_string(nodes) + " at both 10 and 1000 items");
  const std::size_t pool = s.window->widgets().at(s.list).list_pool.size();
  check(pool < static_cast<std::size_t>(kItemCount), "the pool is " + std::to_string(pool) +
                                                         " rows, not one node per item");

  // Claim 2: no residue, forward, after a jump, and backward.
  const dg::PixelRect list = drive.bounds(s.list);
  const int cx = list.x + (list.width / 2);
  const int cy = list.y + (list.height / 2);
  bool clean = exact_content(s, out, "at rest");
  for (int step = 0; step < 40; ++step) {
    drive.wheel(cx, cy, -1.0F);
  }
  clean = exact_content(s, out, "after 40 forward steps") && clean;
  drive.wheel(cx, cy, -900.0F * kItemHeight / kWheelStepPx);
  clean = exact_content(s, out, "after a forward jump") && clean;
  for (int step = 0; step < 60; ++step) {
    drive.wheel(cx, cy, 1.0F);
  }
  clean = exact_content(s, out, "after scrolling back 60 steps") && clean;
  drive.wheel(cx, cy, 100000.0F);
  clean = exact_content(s, out, "back at the very top") && clean;
  check(clean, "every recycled row's text exactly matches its current item");

  // Claim 3: overscroll clamps at 0 and at the last item.
  drive.wheel(cx, cy, 20.0F);
  check(s.window->scroll_offset(s.list).value().y == 0, "scrolling up from rest clamps to 0");
  drive.wheel(cx, cy, -100000.0F);
  const int max_offset = (kItemCount * kItemHeight) - kRowViewport;
  check(s.window->scroll_offset(s.list).value().y == max_offset,
        "scrolling far past the bottom clamps at exactly " + std::to_string(max_offset));
  drive.wheel(cx, cy, 100000.0F);

  // Claim 4: the scrollbar sits in a reserved gutter; dragging its thumb to
  // the end shows the last items exactly; a track click pages back.
  const dg::Widget& widget = s.window->widgets().at(s.list);
  const dg::PixelRect track = drive.bounds(widget.scrollbar_track);
  const dg::PixelRect thumb = drive.bounds(widget.scrollbar_thumb);
  const int gutter = s.window->widgets().scrollbar_style().size;
  check(gutter > 0 && track == dg::PixelRect{list.right() - kListFrame - gutter, list.y + kListFrame, gutter,
                                       list.height - (2 * kListFrame)},
        "the track is the list's right-hand gutter, inside the frame");
  check(drive.bounds(widget.list_pool.front()).right() <= track.x,
        "no list item reaches under the scrollbar");
  check(thumb.height == s.window->widgets().scrollbar_style().min_thumb_length &&
            thumb.y == list.y + kListFrame,
        "the thumb is its minimum length, at the top");
  const dg::PixelPoint grab{thumb.x + 2, thumb.y + 2};
  drive.press(grab.x, grab.y, true);
  // Past the end of the track (but inside the window, where the pointer
  // still reports motion): the thumb saturates at the bottom.
  drive.move(grab.x, grab.y + track.height);
  drive.press(grab.x, grab.y + track.height, false);
  check(s.window->scroll_offset(s.list).value().y == max_offset,
        "dragging the thumb to the end reaches the last item");
  check(drive.bounds(widget.scrollbar_thumb).bottom() == track.bottom(),
        "the thumb reaches the bottom of the track");
  check(exact_content(s, out, "after thumb drag to end"), "no residue after the drag");
  drive.click(track.x + 2, track.y + 2);
  check(s.window->scroll_offset(s.list).value().y == max_offset - kRowViewport,
        "a track click pages up by one viewport");

  out << (ok ? "PASS\n" : "FAIL\n");
  return ok ? 0 : 1;
}

int dump_png(dg::App& app, const Options& options, std::ostream& out) {
  const Scene s = open(app, options.size, kItemCount);
  if (s.window == nullptr) {
    return 1;
  }
  example::Driver drive(app, *s.window);
  drive.pump();
  if (options.jump_items > 0) {
    const dg::PixelRect list = drive.bounds(s.list);
    drive.wheel(list.x + 10, list.y + 10,
                -static_cast<float>(options.jump_items * kItemHeight) / kWheelStepPx);
  }
  return example::write_png(*s.window, options.png_path, out);
}

int run_window(dg::App& app, const Options& options) {
  const Scene s = open(app, options.size, kItemCount);
  if (s.window == nullptr) {
    return 1;
  }
  example::close_after(app, options.run_ms);
  app.run([&](const dg::Event& event) {
    if (dg::event_as<dg::events::TimerEvent>(event) != nullptr) {
      app.quit();
    } else if (const auto* select = dg::event_as<dg::events::ListSelectEvent>(event)) {
      (void)s.window->set_title("drawgui list - " + item_text(select->index));
    }
  });
  return 0;
}

std::optional<int> parse_int(const std::string& text) {
  int value = 0;
  const char* end = text.data() + text.size();
  const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end || value < 0) {
    return std::nullopt;
  }
  return value;
}

std::optional<Options> parse(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (arg == "--verify-list") {
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
    } else if (arg == "--size") {
      const std::size_t x = value.find('x');
      const std::optional<int> w = parse_int(value.substr(0, x));
      const std::optional<int> h =
          x == std::string::npos ? std::nullopt : parse_int(value.substr(x + 1));
      if (!w || !h || *w < 1 || *h < 1) {
        return std::nullopt;
      }
      options.size = {*w, *h};
    } else if (arg == "--run-ms" || arg == "--jump") {
      const std::optional<int> number = parse_int(value);
      if (!number) {
        return std::nullopt;
      }
      (arg == "--run-ms" ? options.run_ms : options.jump_items) = *number;
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
    std::cerr << "usage: drawgui_list [--size WxH] [--run-ms N] [--jump N]\n"
              << "                    [--font-dir DIR] [--dump-png FILE] [--verify-list]\n";
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
      return dump_png(app.value(), *options, std::cout);
    case Mode::kWindow:
      break;
  }
  return run_window(app.value(), *options);
}
