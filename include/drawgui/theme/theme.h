// Theme tokens - the runtime half of design.md section 5.7's two-stage
// design (schema is a compile-time contract, § 5.7.1; this file is the
// runtime storage a resolved schema+data pair produces).
//
// TokenType mirrors PropType (include/drawgui/props/node_props.h) in
// spirit: it is the type-check a token binding validates against, generated
// from the SAME source-of-truth family shape (themes/schema.toml's `type`
// column, exactly as PropType's enumerators are pasted from
// props/drawgui.props.toml's `type` column).
//
// THE TWO-VARIANT DESIGN FOLLOWS design.md section 5.7.4's OWN JSON SHAPE,
// not an invention of this slice: `int` tokens live in theme.json's "base"
// (variant-independent - a radius does not change between light and dark),
// `color` tokens live in "variants.light"/"variants.dark" (design.md's own
// example: "base": { "radius.md": 8 }, "variants": { "light": {...},
// "dark": {...} }). Storage is a flat vector indexed by token_id, not a
// hash map - design.md section 5.15.3's anti-per-node-hashmap argument
// applies just as well to a table this small and this rarely read (once
// per bind, once per theme switch, never once per frame).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "drawgui/graphics/types.h"
#include "drawgui/theme/token_ids.generated.h"

namespace dg {

// THE TABLE `type` TOKENS in themes/schema.toml bind to, the identical
// convention prop_ids.generated.h documents for PropType: the un-house-styled
// `k_` spelling is deliberate, because prop_dispatch-style generated code
// pastes the schema's own `type` string directly onto `TokenType::k_##type`
// (see token_registry.cpp) - a token type added to the schema without a
// matching enumerator here is a compile error, not a silent gap.
enum class TokenType : std::uint8_t {
  k_color,
  k_int,
};

// The declared type of `id`, or nothing when no token has that id. Answers
// for every id the SCHEMA defines, independent of what any particular
// loaded Theme happens to provide a value for - the schema/data split
// design.md section 5.7.1 draws applies here exactly as prop_type()
// answers for the property TABLE rather than for what any one node has set.
[[nodiscard]] std::optional<TokenType> token_type(dg_token_id id);

// The dotted schema name for `id` ("color.surface"), for a diagnostic
// message - the reverse of theme_loader.cpp's name->id resolution.
[[nodiscard]] std::optional<std::string_view> token_name(dg_token_id id);

// The forward direction: a theme.json author's string, resolved to the
// generated numeric id. std::nullopt means the name matches no schema
// token at all - design.md section 5.7.5's "unknown token name" case,
// which the LOADER turns into a load failure naming the exact key path
// (theme_loader.h), never a silent skip.
[[nodiscard]] std::optional<dg_token_id> token_id_for_name(std::string_view name);

// light vs. dark - design.md section 5.7.2's "variant" axis. Only these two
// exist; a third (design.md's own example, "high-contrast") is future work
// this slice does not build, named rather than left ambiguous.
enum class ThemeVariant : std::uint8_t {
  kLight,
  kDark,
};

// One fully-resolved theme: every `int` token's single (variant-independent)
// value, and every `color` token's value under each of the two variants.
//
// STORAGE IS THREE FLAT VECTORS INDEXED BY dg_token_id, sized
// kDgTokenMaxId + 1 - not a map, for the reason given in the file header.
// `has_*` is a separate parallel vector rather than an `optional<Color>` /
// `optional<int>` per slot, so the common "every schema token IS present"
// case (which tools/check_consistency.py enforces for the builtin theme in
// CI) reads a plain value with no per-access branch on the hot resolve
// path (ThemeBindings::apply, theme_bindings.cpp); the `has_*` vectors exist
// so a PARTIAL theme (one CI never sees, but the loader itself must still
// handle without reading uninitialised slots) reports a clean "no value"
// rather than a stale zero.
class Theme {
 public:
  Theme() = default;

  [[nodiscard]] const std::string& name() const { return name_; }
  void set_name(std::string name) { name_ = std::move(name); }

  void set_int(dg_token_id id, int value);
  [[nodiscard]] std::optional<int> int_value(dg_token_id id) const;

  void set_color(dg_token_id id, ThemeVariant variant, Color value);
  [[nodiscard]] std::optional<Color> color_value(dg_token_id id, ThemeVariant variant) const;

 private:
  void ensure_capacity(dg_token_id id);

  std::string name_;
  std::vector<int> ints_;
  std::vector<bool> has_int_;
  std::vector<Color> light_;
  std::vector<bool> has_light_;
  std::vector<Color> dark_;
  std::vector<bool> has_dark_;
};

}  // namespace dg
