// Shortcut/intent generator family (8-1, design.md sections 5.5.1-5.5.3):
// action_id, LogicalKey, ActionScope, the binding table, and the chord
// parser/formatter that proves the `Mod` substitution rule without a
// macOS machine. Deliberately does NOT touch routing, KeyEvent, or the C
// ABI - none of that exists yet (8-2/8-3/8-3d).
//
// `dg_shortcut_label`'s ABI export is declined for this slice by name (see
// include/drawgui/shortcuts/chord.h's own comment); shortcut_label() below
// is the plain-C++ function this slice DOES build, so it is tested here
// rather than deferred silently.

#include <optional>
#include <string>

#include <doctest/doctest.h>

#include "drawgui/shortcuts/action_ids.generated.h"
#include "drawgui/shortcuts/action_scope.generated.h"
#include "drawgui/shortcuts/chord.h"
#include "drawgui/shortcuts/logical_key.generated.h"

namespace {

using dg::Chord;
using dg::LogicalKey;
using dg::Modifier;
using dg::Platform;

}  // namespace

TEST_CASE("Mod substitution resolves to Ctrl on linux/windows and Cmd on macos") {
  // design.md section 5.5.1: `Mod` is macOS's Cmd, everyone else's Ctrl.
  // This is the one part of a three-platform binding table this project
  // can prove today with only a Linux backend built.
  CHECK(dg::mod_label(Platform::kLinux) == "Ctrl");
  CHECK(dg::mod_label(Platform::kWindows) == "Ctrl");
  CHECK(dg::mod_label(Platform::kMacos) == "Cmd");
}

TEST_CASE("parse_chord round-trips a modifier combination") {
  const auto chord = dg::parse_chord("Mod+Shift+C");
  REQUIRE(chord.has_value());
  CHECK(dg::has(chord->mods, Modifier::kMod));
  CHECK(dg::has(chord->mods, Modifier::kShift));
  CHECK_FALSE(dg::has(chord->mods, Modifier::kAlt));
  CHECK(chord->key == LogicalKey::kC);

  const auto bare_key = dg::parse_chord("PageUp");
  REQUIRE(bare_key.has_value());
  CHECK(bare_key->mods == Modifier::kNone);
  CHECK(bare_key->key == LogicalKey::kPageUp);
}

TEST_CASE("parse_chord rejects malformed input") {
  CHECK_FALSE(dg::parse_chord("").has_value());             // empty
  CHECK_FALSE(dg::parse_chord("Mod+").has_value());         // trailing '+', no key
  CHECK_FALSE(dg::parse_chord("+C").has_value());           // leading '+', empty segment
  CHECK_FALSE(dg::parse_chord("Shift+Mod+C").has_value());  // scrambled order
  CHECK_FALSE(dg::parse_chord("Mod+Mod+C").has_value());    // repeated modifier
  CHECK_FALSE(dg::parse_chord("Mod+Nope").has_value());     // unknown key
  CHECK_FALSE(dg::parse_chord("Ctrl+C").has_value());       // not the `Mod` pseudo-modifier
  CHECK_FALSE(dg::parse_chord("Mod").has_value());          // bare modifier, no key
}

TEST_CASE("generated action_id constants and LogicalKey agree with input/shortcuts.toml") {
  CHECK(DG_ACTION_COPY == 1);
  CHECK(DG_ACTION_CUT == 2);
  CHECK(DG_ACTION_PASTE == 3);
  CHECK(DG_ACTION_SELECT_ALL == 4);
  CHECK(DG_ACTION_SCROLL_PAGE_UP == 5);
  CHECK(DG_ACTION_SCROLL_PAGE_DOWN == 6);
  CHECK(DG_ACTION_SCROLL_TO_START == 7);
  CHECK(DG_ACTION_SCROLL_TO_END == 8);
  CHECK(kDgActionCount == 8);

  const auto bindings = dg::all_shortcut_bindings();
  REQUIRE(bindings.size() == 8);

  const auto find = [&](dg_action_id id) -> const dg::ShortcutBinding& {
    for (const auto& binding : bindings) {
      if (binding.action_id == id) {
        return binding;
      }
    }
    FAIL("action_id not found in the generated binding table");
    return bindings[0];
  };

  const auto& copy = find(DG_ACTION_COPY);
  CHECK(copy.name == "copy");
  CHECK(copy.scope == dg::ActionScope::kTextField);
  CHECK(copy.chord == Chord{Modifier::kMod, LogicalKey::kC});
  CHECK(copy.consumer == "8-4 clipboard");

  const auto& scroll_up = find(DG_ACTION_SCROLL_PAGE_UP);
  CHECK(scroll_up.name == "scroll_page_up");
  CHECK(scroll_up.scope == dg::ActionScope::kApp);
  CHECK(scroll_up.chord == Chord{Modifier::kNone, LogicalKey::kPageUp});
  CHECK(scroll_up.consumer == "8-3c keyboard scrolling");
}

TEST_CASE("shortcut_label generates the accelerator text from the table, never hand-written") {
  const auto linux_label = dg::shortcut_label(DG_ACTION_COPY, Platform::kLinux);
  REQUIRE(linux_label.has_value());
  CHECK(*linux_label == "Ctrl+C");

  const auto macos_label = dg::shortcut_label(DG_ACTION_COPY, Platform::kMacos);
  REQUIRE(macos_label.has_value());
  CHECK(*macos_label == "\u2318C");

  const auto scroll_label = dg::shortcut_label(DG_ACTION_SCROLL_TO_START, Platform::kWindows);
  REQUIRE(scroll_label.has_value());
  CHECK(*scroll_label == "Home");

  // An id no binding table entry names - std::nullopt, not a crash or a
  // made-up label. 8-3d's real ABI-facing dg_shortcut_label will need this
  // same "unknown id" shape once a host can pass in an arbitrary uint16_t.
  CHECK_FALSE(dg::shortcut_label(0xFFFF, Platform::kLinux).has_value());
}

TEST_CASE("no two actions share a chord within the same scope") {
  // design.md section 7's own duplicate-binding check, re-proven here
  // against the LIVE generated table rather than only at generation time -
  // tools/gen_shortcuts.py already refuses to emit a table that fails
  // this, so a pass here is a second, independent witness of the same
  // property tools/shortcut_lock_selftest.py's "duplicate chord" case
  // proves the generator rejects when it is violated.
  const auto bindings = dg::all_shortcut_bindings();
  for (std::size_t i = 0; i < bindings.size(); ++i) {
    for (std::size_t j = i + 1; j < bindings.size(); ++j) {
      const bool same_scope = bindings[i].scope == bindings[j].scope;
      const bool same_chord = bindings[i].chord == bindings[j].chord;
      CHECK_FALSE((same_scope && same_chord));
    }
  }
}
