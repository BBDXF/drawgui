// Loading a theme.json into a resolved dg::Theme.
//
// design.md section 5.7.5's two hard rules, both routed through dg::Expected
// rather than a bool/log-and-continue - "silent failure is the same class
// of problem as stonegui's four-file token drift" is this slice's own
// framing of the identical argument design.md already makes:
//
//   UNKNOWN TOKEN NAME -> the load fails and reports the specific location
//   (a JSON key path, "variants.light.color.mystery"), never silently
//   ignored.
//
//   TYPE MISMATCH -> the schema declares each token's type (color/int); a
//   value of the wrong shape (a number where a colour string was expected,
//   or a token used in the wrong section entirely - an int token inside
//   "variants", a colour token inside "base") is an error, not a coercion.
//
// SCOPE, matching this slice's own task boundary: NOT built here are theme
// PACKAGES (a directory with fonts/icons/images - design.md section 5.7.4's
// "mytheme/" layout), path-traversal defence (meaningful only for an
// untrusted EXTERNAL package; the one theme this slice loads is compiled
// into the binary - see doc/theme.md), SVG/icon caching, and hot reload
// (design.md section 5.7.6, explicitly P7). What IS built is the loader
// core every one of those future features would still need: JSON parsing
// bounded against a parse bomb (mini_json.h), schema-checked token
// resolution, and the light/dark colour table this slice's runtime switch
// reads.

#pragma once

#include <string_view>

#include "drawgui/base/expected.h"
#include "drawgui/theme/theme.h"

namespace dg {

enum class ThemeLoadStatus : std::uint8_t {
  // The JSON itself did not parse - see mini_json::ParseError.
  kParseError,

  // The document parsed but is missing a required field, or a required
  // field has the wrong JSON shape ("schema_version" is not a number,
  // "variants" is not an object, ...).
  kMissingField,

  // design.md section 5.7.5: a key inside "base" or "variants.<name>" names
  // no token in themes/schema.toml at all.
  kUnknownToken,

  // design.md section 5.7.5: the key names a real schema token, but either
  // the JSON value's shape is wrong for that token's declared type (an
  // int token needs a JSON number; a color token needs a "#RRGGBBAA"
  // string), or the token was used in the wrong section (an int token
  // inside "variants", a color token inside "base").
  kTypeMismatch,

  // "schema_version" is a number this loader does not understand - the
  // identical un-silent-guard props/drawgui.props.toml's own generator
  // already applies to ITS schema_version field, applied here to the data
  // side instead of the schema side.
  kUnsupportedSchemaVersion,
};

struct ThemeLoadError {
  ThemeLoadStatus status = ThemeLoadStatus::kParseError;

  // Carries the JSON key path ("variants.dark.color.mystery-token") or the
  // byte offset for a parse error - design.md section 5.7.5's "report out
  // the specific location", mirroring PropWrite::message's node-path
  // convention on the property side (node_props.h).
  std::string message;
};

// Parses and fully validates `json_text` against themes/schema.toml's
// generated token table (theme.cpp's token_type()/token_id_for_name()).
[[nodiscard]] Expected<Theme, ThemeLoadError> load_theme(std::string_view json_text);

// The one theme this project ships, embedded as a string
// (include/drawgui/theme/builtin_theme.generated.h) rather than read from a
// file next to the executable - design.md section 5.7.4: "内置主题同样是
// JSON，以字符串嵌入二进制". tools/check_consistency.py verifies, at CI
// time, that it covers every schema token; this function is the SAME
// load_theme() a future external theme package would go through, so the
// builtin theme dogfoods the one loading path rather than taking a
// shortcut around it.
[[nodiscard]] Expected<Theme, ThemeLoadError> load_builtin_theme();

}  // namespace dg
