// Shortcut chords: the parsing/formatting half of design.md section 5.5.1
// that pairs with the generated tables in shortcuts/. Two independent
// things share this file because they share a vocabulary, not because one
// depends on the other at runtime:
//
//   PARSING (parse_chord) reads the SAME grammar tools/gen_shortcuts.py
//   validates at generation time ("Mod"/"Shift"/"Alt", in that order,
//   joined by '+', then a LogicalKey name) - kept as a second, independent
//   implementation rather than shared code across the Python/C++ boundary,
//   the same way prop_id's NAME_PATTERN is re-validated in both languages
//   rather than shared.
//
//   FORMATTING (mod_label/format_chord/shortcut_label) is design.md section
//   5.5.1's own requirement: "绑定表是单一真相源... 加速键文本... 由绑定表
//   生成，不手写". Both read src/shortcuts/binding_table.generated.inc,
//   never a hand-maintained second copy.
//
// Neither function touches include/drawgui/window/window_manager.h's
// KeyEvent, and neither is exported through the C ABI - both are 8-2 and
// 8-3d's job respectively, declined here by name (see input/shortcuts.toml
// and doc/design.md section 5.5.3's dg_shortcut_label signature). This
// slice builds only the generated-id infrastructure and the pure data
// transform (Mod substitution, chord parsing) that infrastructure needs to
// be provably correct without either of those.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "drawgui/shortcuts/action_ids.generated.h"
#include "drawgui/shortcuts/action_scope.generated.h"
#include "drawgui/shortcuts/logical_key.generated.h"

namespace dg {

// Which of the three desktop platforms design.md section 5.5.1's `Mod`
// pseudo-modifier resolves against. Only `kLinux` has a real backend
// (README's "no platform abstraction ahead of a second backend"); the other
// two exist here purely as the argument the substitution rule takes, which
// is what makes the rule provable today without a macOS or Windows
// machine - see tests/unit/test_shortcuts.cpp.
enum class Platform : std::uint8_t {
  kLinux,
  kWindows,
  kMacos,
};

// A physical-key-independent modifier set. `kMod` is design.md 5.5.1's own
// pseudo-modifier - it is NOT Ctrl or Cmd itself. Resolving it against a
// REAL KeyEvent's modifier bits needs the bitmask 8-2 adds to KeyEvent;
// until then it only ever becomes DISPLAY text (mod_label/format_chord
// below). A bitmask, not three bool fields on Chord, because a binding can
// combine more than one (e.g. "Mod+Shift+C").
enum class Modifier : std::uint8_t {
  kNone = 0,
  kMod = 1U << 0U,
  kShift = 1U << 1U,
  kAlt = 1U << 2U,
};

constexpr Modifier operator|(Modifier lhs, Modifier rhs) {
  return static_cast<Modifier>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

constexpr Modifier operator&(Modifier lhs, Modifier rhs) {
  return static_cast<Modifier>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
}

constexpr bool has(Modifier set, Modifier bit) {
  return (set & bit) != Modifier::kNone;
}

// One parsed chord: a modifier set plus the logical key it fires on.
// Field-wise equality is what the duplicate-binding check (design.md
// section 7, enforced at generation time by tools/gen_shortcuts.py) and
// tests/unit/test_shortcuts.cpp both need.
struct Chord {
  Modifier mods = Modifier::kNone;
  LogicalKey key = LogicalKey::kInvalid;

  friend bool operator==(const Chord&, const Chord&) = default;
};

// One row of src/shortcuts/binding_table.generated.inc, exposed to C++
// rather than left as raw initializer syntax only chord.cpp sees.
struct ShortcutBinding {
  dg_action_id action_id;
  ActionScope scope;
  Chord chord;
  std::string_view name;
  std::string_view consumer;
};

// Every generated binding, in input/shortcuts.toml's own order. Used by
// shortcut_label() below and by tests/unit/test_shortcuts.cpp to prove the
// generated tables agree with the source of truth.
[[nodiscard]] std::span<const ShortcutBinding> all_shortcut_bindings();

// Parses "Mod+Shift+C"-style text into a Chord, or std::nullopt for
// anything that is not exactly that grammar: an empty string, an unknown
// modifier or key name, a modifier out of "Mod, Shift, Alt" order, a
// repeated modifier, a trailing '+', or a bare modifier with no key. Only
// accepts key names src/shortcuts/logical_key_table.generated.inc actually
// lists - so this parser accepts exactly the keys a real binding
// referenced, never a speculative superset.
[[nodiscard]] std::optional<Chord> parse_chord(std::string_view text);

// design.md section 5.5.1: "Mod" is macOS's Cmd, everyone else's Ctrl. The
// substitution rule itself - the only part of the platform table that is
// unit-testable without a macOS machine.
[[nodiscard]] std::string_view mod_label(Platform platform);

// The menu-accelerator text design.md section 5.5.1 requires to be
// GENERATED, never hand-written: "Ctrl+Shift+C" on linux/windows,
// "\u21e7\u2318C" (Shift-then-Command glyphs, Apple's own ordering) on
// macos - built from the Chord alone, so a caller can label a binding
// before 8-3d gives it a real action_id lookup.
[[nodiscard]] std::string format_chord(const Chord& chord, Platform platform);

// design.md section 5.5.3's `dg_shortcut_label(action_id)` ABI signature,
// as a plain C++ function rather than an extern "C" export - the ABI
// export itself is declined for THIS slice (8-3d owns abi/drawgui.def.toml
// and its append-only lock; wiring a new export there ahead of 8-3d's
// router would repeat the "twelve deleted platform headers" mistake
// doc/README already records). Returns std::nullopt for an action_id
// all_shortcut_bindings() does not know.
[[nodiscard]] std::optional<std::string> shortcut_label(dg_action_id action_id,
                                                        Platform platform);

}  // namespace dg
