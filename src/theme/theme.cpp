// Theme token registry and Theme storage - see include/drawgui/theme/theme.h.

#include "drawgui/theme/theme.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace dg {
namespace {

struct TokenEntry {
  std::string_view name;
  dg_token_id id;
  TokenType type;
};

// One entry per themes/schema.toml token, generated. This is the ONE place
// the dotted string name appears in generated C++ - see token_table.
// generated.inc's own header comment for why that closes the drift class.
// The .inc contains plain `TokenEntry` initializers (not an X-macro): see
// tools/gen_theme.py's render_table() docstring for why - unlike
// node_props.cpp's DG_PROP_ASSIGN (a full statement, pasted into a
// `switch`), this table has one reader and no control-flow shape to paste
// into, so the macro indirection would have been pure overhead, and
// clang-tidy's cppcoreguidelines-macro-usage agrees.
constexpr std::array<TokenEntry, kDgTokenCount> kTokenTable = {{
#include "theme/token_table.generated.inc"
}};

}  // namespace

std::optional<TokenType> token_type(dg_token_id id) {
  for (const TokenEntry& entry : kTokenTable) {
    if (entry.id == id) {
      return entry.type;
    }
  }
  return std::nullopt;
}

std::optional<std::string_view> token_name(dg_token_id id) {
  for (const TokenEntry& entry : kTokenTable) {
    if (entry.id == id) {
      return entry.name;
    }
  }
  return std::nullopt;
}

std::optional<dg_token_id> token_id_for_name(std::string_view name) {
  for (const TokenEntry& entry : kTokenTable) {
    if (entry.name == name) {
      return entry.id;
    }
  }
  return std::nullopt;
}

void Theme::ensure_capacity(dg_token_id id) {
  const std::size_t needed = static_cast<std::size_t>(id) + 1;
  if (ints_.size() < needed) {
    ints_.resize(needed, 0);
    has_int_.resize(needed, false);
    light_.resize(needed, Color{});
    has_light_.resize(needed, false);
    dark_.resize(needed, Color{});
    has_dark_.resize(needed, false);
  }
}

void Theme::set_int(dg_token_id id, int value) {
  ensure_capacity(id);
  ints_[id] = value;
  has_int_[id] = true;
}

std::optional<int> Theme::int_value(dg_token_id id) const {
  if (id >= has_int_.size() || !has_int_[id]) {
    return std::nullopt;
  }
  return ints_[id];
}

void Theme::set_color(dg_token_id id, ThemeVariant variant, Color value) {
  ensure_capacity(id);
  if (variant == ThemeVariant::kLight) {
    light_[id] = value;
    has_light_[id] = true;
  } else {
    dark_[id] = value;
    has_dark_[id] = true;
  }
}

std::optional<Color> Theme::color_value(dg_token_id id, ThemeVariant variant) const {
  const std::vector<bool>& has = variant == ThemeVariant::kLight ? has_light_ : has_dark_;
  const std::vector<Color>& values = variant == ThemeVariant::kLight ? light_ : dark_;
  if (id >= has.size() || !has[id]) {
    return std::nullopt;
  }
  return values[id];
}

}  // namespace dg
