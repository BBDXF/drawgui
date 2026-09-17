// The scene examples/12_text_input puts on screen, and the handles a check
// needs.
//
// TWO TextFields, sharing one kTextField mechanism and one dg::Focus:
//
//   field_a   pre-filled with a string wider than the field - the overflow
//             case. Unfocused it shows an ellipsis-truncated prefix; focused
//             it shows the full string scrolled to keep the cursor visible.
//
//   field_b   starts empty - the "type from scratch" case, and the one that
//             proves clicking b blurs a (dg::Focus is exclusive: at most one
//             focused widget, doc/text-input.md section 3).
//
// Both are built from the same primitives every widget in this project
// already stands on: a kLeaf with `overflow: kClip`, three plain children
// (selection highlight, text content, caret) the widget positions - no new
// RenderObject, matching design.md section 5.6 line 622's acceptance bar the
// same way doc/form-controls.md already proved it for Slider.

#pragma once

#include <optional>
#include <string>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

namespace text_field_scene {

inline constexpr int kFieldWidth = 260;
inline constexpr int kFieldHeight = 36;
inline constexpr int kFontSize = 16;

inline constexpr const char* kFieldAInitial = "The quick brown fox jumps over the lazy dog";

struct Handles {
  dg::NodeId body;
  dg::NodeId field_a;
  dg::NodeId field_b;
};

struct Options {
  dg::TreeSpec spec;
  std::string font_dir = "/usr/share/fonts";
};

struct Scene {
  dg::LayoutTree tree;
  dg::WidgetSet widgets;
  dg::Focus focus;
  Handles handles;

  // Each field scopes its own copy/cut/paste/select_all onto itself (8-4,
  // input/shortcuts.toml's own "a TextField scoping both select_all and
  // paste" example - action_scopes.h's own header comment names this
  // exact scene shape) - the router's level 2 is what lets a real Mod+C
  // over a focused field resolve at all; these four actions bind at
  // ActionScope::kTextField with no app-level fallback, so a field that
  // never scopes them can never fire one.
  dg::ActionScopes action_scopes;

  // The scene's OWN copy of the catalog RenderTree paints with - FontCatalog
  // is a cheap, reference-counted handle to one table (its own header:
  // "Copyable, and a copy names the same fonts"), so this is not a second
  // table that could disagree, only a second reference to the same one.
  // WidgetSet's text_field_* calls need it directly (measuring a caret
  // position or a click offset) and RenderTree exposes no getter back for
  // the copy TreeSpec consumed - doc/text-input.md section 4.
  std::optional<dg::FontCatalog> fonts;
};

Scene build(const Options& options);

[[nodiscard]] std::string describe(const Scene& scene, dg::NodeId id);

// Focuses `target` (or blurs everything when std::nullopt), applying the
// focused/unfocused display mode to whichever field(s) the change touched -
// the same "one atomic change, both widgets refreshed" shape
// doc/scrolling.md and doc/form-controls.md already use for their own
// transitions.
void set_focus(Scene& scene, std::optional<dg::NodeId> target);

}  // namespace text_field_scene
