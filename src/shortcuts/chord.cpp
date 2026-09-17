#include "drawgui/shortcuts/chord.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace dg {
namespace {

// name -> LogicalKey, generated from exactly the keys input/shortcuts.toml
// references (tools/gen_shortcuts.py's render_logical_key_table()).
struct KeyNameEntry {
  std::string_view name;
  LogicalKey key;
};

constexpr std::array<KeyNameEntry, kLogicalKeyCount> kKeyNames = {{
#include "shortcuts/logical_key_table.generated.inc"
}};

constexpr std::optional<LogicalKey> lookup_key(std::string_view name) {
  for (const auto& entry : kKeyNames) {
    if (entry.name == name) {
      return entry.key;
    }
  }
  return std::nullopt;
}

// The fixed modifier grammar - NOT generated, the same "engineering
// vocabulary, not table data" distinction tools/gen_shortcuts.py's own
// MODIFIER_ORDER comment draws. Order matters: parse_chord() rejects any
// binding whose modifiers do not appear in this exact sequence, the same
// rule the generator enforces on input/shortcuts.toml.
constexpr std::array<std::pair<std::string_view, Modifier>, 3> kModifierOrder = {{
    {"Mod", Modifier::kMod},
    {"Shift", Modifier::kShift},
    {"Alt", Modifier::kAlt},
}};

const std::array<ShortcutBinding, kDgActionCount> kBindingTable = {{
#include "shortcuts/binding_table.generated.inc"
}};

}  // namespace

std::span<const ShortcutBinding> all_shortcut_bindings() {
  return kBindingTable;
}

std::optional<Chord> parse_chord(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }

  Modifier mods = Modifier::kNone;
  std::size_t next_modifier = 0;  // index into kModifierOrder, enforces order
  std::string_view remaining = text;

  for (;;) {
    const auto plus = remaining.find('+');
    const std::string_view segment =
        plus == std::string_view::npos ? remaining : remaining.substr(0, plus);
    if (segment.empty()) {
      return std::nullopt;  // empty '+'-separated segment, or a trailing '+'
    }
    if (plus == std::string_view::npos) {
      // The final segment: the logical key, not a modifier.
      const auto key = lookup_key(segment);
      if (!key.has_value()) {
        return std::nullopt;
      }
      return Chord{mods, *key};
    }

    // Every non-final segment must be the next unseen modifier in fixed
    // order - rejects both an unknown word and a scrambled/repeated one.
    if (next_modifier >= kModifierOrder.size() ||
        kModifierOrder[next_modifier].first != segment) {
      return std::nullopt;
    }
    mods = mods | kModifierOrder[next_modifier].second;
    ++next_modifier;
    remaining = remaining.substr(plus + 1);
  }
}

std::string_view mod_label(Platform platform) {
  switch (platform) {
    case Platform::kLinux:
    case Platform::kWindows:
      return "Ctrl";
    case Platform::kMacos:
      return "Cmd";
  }
  return "Ctrl";  // unreachable: every Platform enumerator is handled above
}

std::string format_chord(const Chord& chord, Platform platform) {
  const std::string_view key_name = [&] {
    for (const auto& entry : kKeyNames) {
      if (entry.key == chord.key) {
        return entry.name;
      }
    }
    return std::string_view{"?"};
  }();

  if (platform == Platform::kMacos) {
    // Apple's own glyph order: Shift, Option, Command, then the key - never
    // '+'-joined. design.md section 5.5.1's own example is "⌘C" (Mod alone);
    // Shift/Alt are included here for format_chord()'s general case even
    // though no action in input/shortcuts.toml uses them yet.
    std::string label;
    if (has(chord.mods, Modifier::kShift)) {
      label += "\u21e7";
    }
    if (has(chord.mods, Modifier::kAlt)) {
      label += "\u2325";
    }
    if (has(chord.mods, Modifier::kMod)) {
      label += "\u2318";
    }
    label += key_name;
    return label;
  }

  std::string label;
  if (has(chord.mods, Modifier::kMod)) {
    label += mod_label(platform);
    label += "+";
  }
  if (has(chord.mods, Modifier::kShift)) {
    label += "Shift+";
  }
  if (has(chord.mods, Modifier::kAlt)) {
    label += "Alt+";
  }
  label += key_name;
  return label;
}

std::optional<std::string> shortcut_label(dg_action_id action_id, Platform platform) {
  for (const auto& binding : kBindingTable) {
    if (binding.action_id == action_id) {
      return format_chord(binding.chord, platform);
    }
  }
  return std::nullopt;
}

}  // namespace dg
