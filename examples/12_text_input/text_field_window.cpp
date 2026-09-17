#include "text_field_window.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

#include "text_field_scene.h"

namespace text_field_window {
namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point started) {
  return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

text_field_scene::Options spec_for(dg::PixelSize size) {
  text_field_scene::Options options;
  options.spec.viewport = size;
  options.spec.background.fill = dg::Color::from_argb(0xFF0E1218);
  return options;
}

void legend(std::ostream& out) {
  out << "  click a field to focus it and place the cursor; type; drag to select;\n"
      << "  Left/Right/Home/End move the cursor, Shift extends a selection;\n"
      << "  Backspace/Delete edit; click elsewhere (or the other field) to blur.\n";
}

void apply_presets(text_field_scene::Scene& scene, const Settings& settings) {
  if (!scene.fonts.has_value()) {
    return;
  }
  const dg::FontCatalog& fonts = *scene.fonts;
  if (!settings.preset_field_b_text.empty()) {
    scene.widgets.text_field_set_focus(scene.tree.render(), fonts, scene.handles.field_b, true);
    scene.widgets.text_field_insert(scene.tree.render(), fonts, scene.handles.field_b,
                                    settings.preset_field_b_text);
    if (!settings.preset_focus_field_a) {
      scene.widgets.text_field_set_focus(scene.tree.render(), fonts, scene.handles.field_b,
                                         false);
    }
  }
  if (settings.preset_focus_field_a) {
    text_field_scene::set_focus(scene, scene.handles.field_a);
  }
  if (settings.preset_select_field_a) {
    dg::RenderTree& tree = scene.tree.render();
    text_field_scene::set_focus(scene, scene.handles.field_a);
    scene.widgets.text_field_move(tree, fonts, scene.handles.field_a,
                                  dg::TextFieldMove::kLineStart, false);
    for (int i = 0; i < 20; ++i) {
      scene.widgets.text_field_move(tree, fonts, scene.handles.field_a,
                                    dg::TextFieldMove::kCharRight, true);
    }
  }
  if (settings.preset_compose_field_b) {
    text_field_scene::set_focus(scene, scene.handles.field_b);
    // "\xE4\xBD\xA0\xE5\xA5\xBD" is "你好" (6 UTF-8 bytes, 2 codepoints) -
    // SDL's own start=2 names the SECOND codepoint's position (matching
    // TextEditingEvent's own "UTF-8 characters" unit), length=1 highlights
    // it as the IME's own focused clause.
    scene.widgets.text_field_composition_update(
        scene.tree.render(), fonts, scene.handles.field_b, "\xE4\xBD\xA0\xE5\xA5\xBD", 2, 1);
  }
}

class Runner {
 public:
  Runner(dg::WindowManager& manager, dg::WindowId window, const Settings& settings,
         std::ostream& out)
      : settings_(settings), out_(&out), manager_(&manager), window_(window) {}

  int run();

 private:
  bool resize_if_needed();
  void draw(text_field_scene::Scene& scene, dg::RasterSurface& surface);
  void handle_down(text_field_scene::Scene& scene, dg::PixelPoint at);
  void handle_move(text_field_scene::Scene& scene, dg::PixelPoint at);
  void handle_up();
  void handle_key(text_field_scene::Scene& scene, const dg::KeyEvent& event);
  static void handle_text(text_field_scene::Scene& scene, const dg::TextInputEvent& event);
  static void handle_text_editing(text_field_scene::Scene& scene,
                                  const dg::TextEditingEvent& event);

  Settings settings_;
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId window_;
  std::optional<text_field_scene::Scene> scene_;
  std::optional<dg::RasterSurface> surface_;

  // Which field a drag is extending a selection over - the same shape
  // examples/11_form_controls's slider drag and examples/10_scrolling's
  // click-drag both already use: a plain optional NodeId set at kDown,
  // consumed at kMove, cleared at kUp/kLeave.
  std::optional<dg::NodeId> dragging_;
};

bool Runner::resize_if_needed() {
  const dg::Expected<dg::PixelSize, dg::WindowError> size = manager_->drawable_size(window_);
  if (!size) {
    return false;
  }
  const dg::PixelSize pixels = size.value();
  if (scene_.has_value()) {
    text_field_scene::Scene& scene = *scene_;
    if (surface_.has_value() && scene.tree.viewport() == pixels) {
      return true;
    }
    scene.tree.resize(pixels);
    scene.tree.layout();
  } else {
    scene_ = text_field_scene::build(spec_for(pixels));
    apply_presets(*scene_, settings_);
    // SDL generates no SDL_EVENT_TEXT_INPUT at all until this has been
    // called on the window - see start_text_input()'s own doc comment. It is
    // enabled for the window's whole lifetime rather than toggled per focus
    // change: this engine's OWN focus model, not SDL's IME capture state,
    // decides which widget a committed character reaches.
    manager_->start_text_input(window_, dg::PixelRect{0, 0, 1, 1});
  }

  surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
  return surface_.has_value() && scene_.has_value();
}

void Runner::draw(text_field_scene::Scene& scene, dg::RasterSurface& surface) {
  scene.tree.layout();
  if (scene.tree.render().damage().is_empty()) {
    return;
  }
  scene.tree.render().repaint(surface);

  const dg::PixelView view = surface.peek_pixels();
  if (view.pixels == nullptr || !view.is_bgra8888) {
    return;
  }
  const dg::ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                            dg::PixelFormat::kBgra8888};
  (void)manager_->present(window_, image, scene.tree.render().painted().rects());
}

void Runner::handle_down(text_field_scene::Scene& scene, dg::PixelPoint at) {
  const std::optional<dg::NodeId> hit = scene.tree.render().hit_test(at);
  const std::optional<dg::NodeId> field =
      hit.has_value() ? scene.widgets.owner_of(scene.tree.render(), *hit) : std::nullopt;

  if (!field.has_value() || !scene.widgets.has(*field) ||
      scene.widgets.at(*field).kind != dg::WidgetKind::kTextField) {
    text_field_scene::set_focus(scene, std::nullopt);
    dragging_.reset();
    return;
  }

  text_field_scene::set_focus(scene, field);
  if (scene.fonts.has_value()) {
    scene.widgets.text_field_click(scene.tree.render(), *scene.fonts, *field, at.x,
                                   /*extend_selection=*/false);
  }
  dragging_ = *field;
}

void Runner::handle_move(text_field_scene::Scene& scene, dg::PixelPoint at) {
  if (!dragging_.has_value() || !scene.fonts.has_value()) {
    return;
  }
  scene.widgets.text_field_click(scene.tree.render(), *scene.fonts, *dragging_, at.x,
                                 /*extend_selection=*/true);
}

void Runner::handle_up() {
  dragging_.reset();
}

void Runner::handle_key(text_field_scene::Scene& scene, const dg::KeyEvent& event) {
  if (event.action != dg::KeyAction::kDown || !scene.fonts.has_value()) {
    return;
  }
  const std::optional<dg::NodeId> focused = scene.focus.current();
  if (!focused.has_value()) {
    return;
  }
  const dg::NodeId field = *focused;
  const dg::FontCatalog& fonts = *scene.fonts;
  dg::RenderTree& tree = scene.tree.render();
  switch (event.key) {
    case dg::Key::kLeft:
      scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kCharLeft,
                                    dg::has(event.mods, dg::Modifier::kShift));
      break;
    case dg::Key::kRight:
      scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kCharRight,
                                    dg::has(event.mods, dg::Modifier::kShift));
      break;
    case dg::Key::kHome:
      scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kLineStart,
                                    dg::has(event.mods, dg::Modifier::kShift));
      break;
    case dg::Key::kEnd:
      scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kLineEnd,
                                    dg::has(event.mods, dg::Modifier::kShift));
      break;
    case dg::Key::kBackspace:
      scene.widgets.text_field_backspace(tree, fonts, field);
      break;
    case dg::Key::kDelete:
      scene.widgets.text_field_delete_forward(tree, fonts, field);
      break;
    case dg::Key::kEscape:
      // 7-3 (doc/ime.md section 6): Escape cancels an in-progress
      // composition without committing it - both halves, this engine's own
      // model AND the platform's real IME state, so neither is left ahead
      // of the other.
      if (scene.widgets.text_field_is_composing(field)) {
        scene.widgets.text_field_cancel_composition(tree, fonts, field);
        manager_->clear_composition(window_);
      }
      break;
    case dg::Key::kTab:
      // Tab/Shift-Tab moving focus is 7-4's own job (examples/21_focus) -
      // this demo predates dg::Focus::focus_next()/focus_previous() and has
      // exactly two tab stops with no non-text-field widget between them,
      // so it is left as a plain field-to-field click-to-focus demo rather
      // than retrofitted here.
    case dg::Key::kUp:
    case dg::Key::kDown:
    case dg::Key::kEnter:
    case dg::Key::kOther:
      break;
  }
}

void Runner::handle_text(text_field_scene::Scene& scene, const dg::TextInputEvent& event) {
  if (!scene.fonts.has_value()) {
    return;
  }
  const std::optional<dg::NodeId> focused = scene.focus.current();
  if (!focused.has_value()) {
    return;
  }
  scene.widgets.text_field_insert(scene.tree.render(), *scene.fonts, *focused, event.text);
}

void Runner::handle_text_editing(text_field_scene::Scene& scene,
                                 const dg::TextEditingEvent& event) {
  if (!scene.fonts.has_value()) {
    return;
  }
  const std::optional<dg::NodeId> focused = scene.focus.current();
  if (!focused.has_value()) {
    return;
  }
  scene.widgets.text_field_composition_update(scene.tree.render(), *scene.fonts, *focused,
                                              event.text, event.start, event.length);
}

int Runner::run() {
  const Clock::time_point started = Clock::now();
  *out_ << "text input demo: click a field, type, select, edit.\n";
  legend(*out_);

  while (manager_->open_window_count() > 0) {
    if (settings_.run_ms > 0 && ms_since(started) >= settings_.run_ms) {
      manager_->request_close(window_);
    }
    const dg::PumpResult pumped = manager_->pump(40);

    const bool stale = !pumped.needs_repaint.empty() || !surface_.has_value();
    if (stale && !resize_if_needed()) {
      return 1;
    }
    if (!scene_.has_value() || !surface_.has_value()) {
      return 1;
    }
    text_field_scene::Scene& scene = *scene_;
    dg::RasterSurface& surface = *surface_;

    for (const dg::PointerEvent& event : pumped.pointer) {
      switch (event.action) {
        case dg::PointerAction::kDown:
          handle_down(scene, dg::PixelPoint{event.x, event.y});
          break;
        case dg::PointerAction::kMove:
          handle_move(scene, dg::PixelPoint{event.x, event.y});
          break;
        case dg::PointerAction::kUp:
        case dg::PointerAction::kLeave:
          handle_up();
          break;
        case dg::PointerAction::kWheel:
          break;
      }
    }
    for (const dg::KeyEvent& event : pumped.key) {
      handle_key(scene, event);
    }
    // Composition-preview events BEFORE committed text, matching the order
    // a real IME delivers them in: SDL_EVENT_TEXT_EDITING (repeatedly, one
    // per keystroke while composing) always precedes the eventual
    // SDL_EVENT_TEXT_INPUT that actually commits.
    for (const dg::TextEditingEvent& event : pumped.text_editing) {
      handle_text_editing(scene, event);
    }
    for (const dg::TextInputEvent& event : pumped.text_input) {
      handle_text(scene, event);
    }
    draw(scene, surface);
  }
  return 0;
}

std::optional<text_field_scene::Scene> rendered(const Settings& settings,
                                                std::optional<dg::RasterSurface>& surface) {
  surface = dg::RasterSurface::create(settings.size.width, settings.size.height);
  if (!surface.has_value()) {
    return std::nullopt;
  }
  text_field_scene::Scene scene = text_field_scene::build(spec_for(settings.size));
  apply_presets(scene, settings);
  scene.tree.layout();
  scene.tree.render().repaint_full(*surface);
  return scene;
}

}  // namespace

int run(const Settings& settings, std::ostream& out) {
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "could not start the window system: " << made.error().message << "\n";
    return 1;
  }
  dg::WindowManager manager = std::move(made.value());

  dg::WindowSpec spec;
  spec.title = "drawgui text input";
  spec.width = settings.size.width;
  spec.height = settings.size.height;
  spec.fill = dg::Color::from_argb(0xFF0E1218);
  dg::Expected<dg::WindowId, dg::WindowError> opened = manager.open(spec);
  if (!opened) {
    out << "could not open a window: " << opened.error().message << "\n";
    return 1;
  }

  Runner runner{manager, opened.value(), settings, out};
  return runner.run();
}

int dump_png(const Settings& settings, const std::string& path, std::ostream& out) {
  std::optional<dg::RasterSurface> surface;
  const std::optional<text_field_scene::Scene> scene = rendered(settings, surface);
  if (!scene.has_value() || !surface.has_value()) {
    out << "could not render a frame\n";
    return 1;
  }
  const std::vector<std::uint8_t> png = surface->encode_png();
  if (png.empty()) {
    out << "could not encode the frame\n";
    return 2;
  }
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(png.data()),
             static_cast<std::streamsize>(png.size()));
  if (!file) {
    out << "could not write " << path << "\n";
    return 3;
  }
  out << "field a: \"" << scene->widgets.text_field_text(scene->handles.field_a) << "\"\n";
  out << "field b: \"" << scene->widgets.text_field_text(scene->handles.field_b) << "\"\n";
  out << "wrote " << path << " (" << png.size() << " bytes)\n";
  return 0;
}

int probe(const Settings& settings, dg::PixelPoint point, std::ostream& out) {
  std::optional<dg::RasterSurface> surface;
  const std::optional<text_field_scene::Scene> scene = rendered(settings, surface);
  if (!scene.has_value() || !surface.has_value()) {
    out << "could not render a frame\n";
    return 1;
  }
  const dg::PixelView view = surface->peek_pixels();
  const std::size_t offset = (static_cast<std::size_t>(point.y) * view.row_bytes) +
                             (static_cast<std::size_t>(point.x) * 4);
  const std::optional<dg::NodeId> hit = scene->tree.render().hit_test(point);
  out << "  at " << point.x << "," << point.y << "  pixel #" << std::hex
      << ((static_cast<unsigned>(view.pixels[offset + 2]) << 16U) |
          (static_cast<unsigned>(view.pixels[offset + 1]) << 8U) |
          static_cast<unsigned>(view.pixels[offset]))
      << std::dec << "  hit "
      << (hit.has_value() ? text_field_scene::describe(*scene, *hit) : std::string{"<nothing>"})
      << "\n";
  return 0;
}

int script(const Settings& settings, std::ostream& out) {
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "could not start the window system: " << made.error().message << "\n";
    return 1;
  }
  dg::WindowManager manager = std::move(made.value());

  dg::WindowSpec spec;
  spec.title = "drawgui text input (scripted)";
  spec.width = settings.size.width;
  spec.height = settings.size.height;
  spec.fill = dg::Color::from_argb(0xFF0E1218);
  dg::Expected<dg::WindowId, dg::WindowError> opened = manager.open(spec);
  if (!opened) {
    out << "could not open a window: " << opened.error().message << "\n";
    return 1;
  }
  const dg::WindowId window = opened.value();

  text_field_scene::Scene scene = text_field_scene::build(spec_for(settings.size));
  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(settings.size.width, settings.size.height);
  if (!surface.has_value()) {
    out << "could not allocate a surface\n";
    return 1;
  }
  scene.tree.render().repaint_full(*surface);
  manager.start_text_input(window, dg::PixelRect{0, 0, 1, 1});

  const dg::PixelRect field_b = scene.tree.bounds(scene.handles.field_b);
  const dg::PixelPoint click_at{field_b.x + 10, field_b.y + (field_b.height / 2)};

  manager.warp_pointer(window, click_at.x, click_at.y);
  (void)manager.pump(50);
  manager.post_pointer_button(window, true, click_at.x, click_at.y);
  manager.post_pointer_button(window, false, click_at.x, click_at.y);
  const dg::PumpResult click_events = manager.pump(200);
  int downs = 0;
  int ups = 0;
  for (const dg::PointerEvent& event : click_events.pointer) {
    downs += event.action == dg::PointerAction::kDown ? 1 : 0;
    ups += event.action == dg::PointerAction::kUp ? 1 : 0;
  }
  out << "  clicked field b at " << click_at.x << "," << click_at.y << ": " << downs
      << " real down event(s), " << ups << " real up event(s) delivered\n";

  // A SYNTHESIZED SDL_EVENT_TEXT_EDITING, pushed the same route
  // post_pointer_button()/post_text_input() already prove out - the
  // deterministic testing answer doc/ime.md section 5 records: a real IME
  // on THIS development machine never produces one at all (it draws its
  // own composition window instead), so this is the only way this project
  // has to exercise the composition-preview code path through the real SDL
  // event queue. Stated plainly, not glossed over: this proves the engine
  // reads a well-formed SDL_EVENT_TEXT_EDITING correctly, NOT that it works
  // end-to-end against a genuine composing IME.
  manager.post_text_editing(window, "h", 1, 0);
  const dg::PumpResult editing_events = manager.pump(200);
  out << "  composed \"h\" (SYNTHESIZED SDL_EVENT_TEXT_EDITING - doc/ime.md section 5: no "
         "real IME on this machine ever produces one): "
      << editing_events.text_editing.size() << " event(s) delivered\n";

  // A real committed-text event AND a real key event, both on the platform's
  // own queue - the same route post_pointer_button() proves out for a click.
  manager.post_text_input(window, "hi");
  const dg::PumpResult text_events = manager.pump(200);
  out << "  typed \"hi\": " << text_events.text_input.size() << " real text-input event(s) "
      << "delivered\n";

  manager.post_key(window, true, dg::Key::kBackspace, false);
  manager.post_key(window, false, dg::Key::kBackspace, false);
  const dg::PumpResult key_events = manager.pump(200);
  int key_downs = 0;
  for (const dg::KeyEvent& event : key_events.key) {
    key_downs += event.action == dg::KeyAction::kDown ? 1 : 0;
  }
  out << "  pressed Backspace: " << key_downs << " real key-down event(s) delivered\n";

  out << "wrote nothing; this mode is for interactive/manual verification\n";
  manager.request_close(window);
  (void)manager.pump(50);
  return 0;
}

}  // namespace text_field_window
