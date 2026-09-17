#include "drawgui/theme/theme_loader.h"

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>

#include "drawgui/theme/builtin_theme.generated.h"
#include "mini_json.h"

namespace dg {
namespace {

[[nodiscard]] Unexpected<ThemeLoadError> fail(ThemeLoadStatus status, std::string message) {
  return Unexpected(ThemeLoadError{status, std::move(message)});
}

// "#RRGGBBAA" - design.md section 5.11.3 rule 1's boundary convention
// ("`#RRGGBBAA` 的主题写法自然对应" the direct 0xAARRGGBB internal value),
// applied literally: eight hex digits after '#', red-green-blue-alpha in
// that order, converted to dg::Color via Color::rgba(r,g,b,a) rather than
// Color::from_argb(), which would require the caller to reorder the bytes
// themselves.
[[nodiscard]] std::optional<Color> parse_hex_color(const std::string& text) {
  if (text.size() != 9 || text[0] != '#') {
    return std::nullopt;
  }
  std::uint8_t channels[4] = {0, 0, 0, 0};
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 2; ++j) {
      const char ch = text[1 + (static_cast<std::size_t>(i) * 2) + static_cast<std::size_t>(j)];
      std::uint8_t nibble = 0;
      if (ch >= '0' && ch <= '9') {
        nibble = static_cast<std::uint8_t>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        nibble = static_cast<std::uint8_t>(ch - 'a' + 10);
      } else if (ch >= 'A' && ch <= 'F') {
        nibble = static_cast<std::uint8_t>(ch - 'A' + 10);
      } else {
        return std::nullopt;
      }
      channels[i] = static_cast<std::uint8_t>((channels[i] << 4) | nibble);
    }
  }
  return Color::rgba(channels[0], channels[1], channels[2], channels[3]);
}

// Parses "base": an object of int-token-name -> JSON number. Every key must
// name a schema token of type k_int; anything else is exactly the two
// failure modes design.md section 5.7.5 names.
[[nodiscard]] Expected<void, ThemeLoadError> load_base(const mini_json::Value& base_value,
                                                       Theme& theme) {
  const mini_json::Object* base = base_value.as_object();
  if (base == nullptr) {
    return fail(ThemeLoadStatus::kMissingField, "'base' must be an object");
  }
  for (const auto& [key, value] : *base) {
    const std::string path = "base." + key;
    const std::optional<dg_token_id> id = token_id_for_name(key);
    if (!id.has_value()) {
      return fail(ThemeLoadStatus::kUnknownToken,
                  path + ": no such token in themes/schema.toml");
    }
    const std::optional<TokenType> type = token_type(*id);
    if (!type.has_value() || *type != TokenType::k_int) {
      std::string message = path;
      message += ": '";
      message += key;
      message += "' is a color token, not int - it belongs under 'variants.<name>', not 'base'";
      return fail(ThemeLoadStatus::kTypeMismatch, std::move(message));
    }
    const double* number = value.as_number();
    if (number == nullptr) {
      return fail(ThemeLoadStatus::kTypeMismatch, path + ": expected a JSON number");
    }
    theme.set_int(*id, static_cast<int>(*number));
  }
  return Expected<void, ThemeLoadError>{};
}

[[nodiscard]] Expected<void, ThemeLoadError> load_variant(const std::string& variant_name,
                                                          const mini_json::Value& variant_value,
                                                          ThemeVariant variant, Theme& theme) {
  const mini_json::Object* colors = variant_value.as_object();
  if (colors == nullptr) {
    std::string message = "'variants.";
    message += variant_name;
    message += "' must be an object";
    return fail(ThemeLoadStatus::kMissingField, std::move(message));
  }
  for (const auto& [key, value] : *colors) {
    std::string path = "variants.";
    path += variant_name;
    path += ".";
    path += key;
    const std::optional<dg_token_id> id = token_id_for_name(key);
    if (!id.has_value()) {
      return fail(ThemeLoadStatus::kUnknownToken,
                  path + ": no such token in themes/schema.toml");
    }
    const std::optional<TokenType> type = token_type(*id);
    if (!type.has_value() || *type != TokenType::k_color) {
      std::string message = path;
      message += ": '";
      message += key;
      message +=
          "' is an int token, not color - it belongs under 'base', not 'variants.<name>'";
      return fail(ThemeLoadStatus::kTypeMismatch, std::move(message));
    }
    const std::string* str = value.as_string();
    if (str == nullptr) {
      return fail(ThemeLoadStatus::kTypeMismatch, path + ": expected a JSON string");
    }
    const std::optional<Color> color = parse_hex_color(*str);
    if (!color.has_value()) {
      return fail(ThemeLoadStatus::kTypeMismatch,
                  path + ": '" + *str + "' is not '#RRGGBBAA' (eight hex digits)");
    }
    theme.set_color(*id, variant, *color);
  }
  return Expected<void, ThemeLoadError>{};
}

}  // namespace

Expected<Theme, ThemeLoadError> load_theme(std::string_view json_text) {
  const Expected<mini_json::Value, mini_json::ParseError> parsed = mini_json::parse(json_text);
  if (!parsed) {
    const mini_json::ParseError& error = parsed.error();
    return fail(ThemeLoadStatus::kParseError,
                "byte " + std::to_string(error.offset) + ": " + error.message);
  }
  const mini_json::Value& root = parsed.value();
  if (root.as_object() == nullptr) {
    return fail(ThemeLoadStatus::kMissingField, "the top level must be a JSON object");
  }

  const mini_json::Value* schema_version = root.find("schema_version");
  if (schema_version == nullptr || schema_version->as_number() == nullptr) {
    return fail(ThemeLoadStatus::kMissingField, "'schema_version' must be a number");
  }
  if (static_cast<int>(*schema_version->as_number()) != 1) {
    return fail(ThemeLoadStatus::kUnsupportedSchemaVersion,
                "schema_version " + std::to_string(*schema_version->as_number()) +
                    " is not supported; this loader understands 1");
  }

  Theme theme;
  const mini_json::Value* name = root.find("name");
  if (name != nullptr) {
    if (name->as_string() == nullptr) {
      return fail(ThemeLoadStatus::kMissingField, "'name' must be a string");
    }
    theme.set_name(*name->as_string());
  }

  const mini_json::Value* base = root.find("base");
  if (base == nullptr) {
    return fail(ThemeLoadStatus::kMissingField, "missing 'base'");
  }
  Expected<void, ThemeLoadError> base_result = load_base(*base, theme);
  if (!base_result) {
    return Unexpected(base_result.error());
  }

  const mini_json::Value* variants = root.find("variants");
  if (variants == nullptr || variants->as_object() == nullptr) {
    return fail(ThemeLoadStatus::kMissingField, "'variants' must be an object");
  }
  bool saw_light = false;
  bool saw_dark = false;
  for (const auto& [variant_name, variant_value] : *variants->as_object()) {
    ThemeVariant variant{};
    if (variant_name == "light") {
      variant = ThemeVariant::kLight;
      saw_light = true;
    } else if (variant_name == "dark") {
      variant = ThemeVariant::kDark;
      saw_dark = true;
    } else {
      // A variant name this loader does not recognise yet (design.md
      // section 5.7.2: "可扩展（如 high-contrast）"). Not an error - the
      // schema/data split means a THEME may offer a variant this build's
      // runtime switch does not read, and rejecting it would make adding a
      // new variant to a theme.json a breaking change for old binaries,
      // exactly backwards from an additive extension.
      continue;
    }
    Expected<void, ThemeLoadError> variant_result =
        load_variant(variant_name, variant_value, variant, theme);
    if (!variant_result) {
      return Unexpected(variant_result.error());
    }
  }
  if (!saw_light || !saw_dark) {
    return fail(ThemeLoadStatus::kMissingField,
                "'variants' must contain both 'light' and 'dark'");
  }

  return theme;
}

Expected<Theme, ThemeLoadError> load_builtin_theme() {
  return load_theme(kBuiltinThemeJson);
}

}  // namespace dg
