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
// SCOPE, matching this slice's own task boundary (6-2): NOT built here are
// theme PACKAGES (a directory with fonts/icons/images - design.md section
// 5.7.4's "mytheme/" layout), path-traversal defence (meaningful only for an
// untrusted EXTERNAL package; the one theme this slice loads is compiled
// into the binary - see doc/theme.md), SVG/icon caching, and hot reload
// (design.md section 5.7.6, explicitly P7). What IS built is the loader
// core every one of those future features would still need: JSON parsing
// bounded against a parse bomb (mini_json.h), schema-checked token
// resolution, and the light/dark colour table this slice's runtime switch
// reads.
//
// 7-6 UPDATE (append-only, doc/theme-packages.md has the full record): the
// four ThemeLoadStatus values named below as this slice's own new work
// (kIoError/kPathTraversal/kResourceTooLarge/kTooManyResources) are exactly
// that closed gap - one EXTENDED vocabulary, not a second one beside it,
// because a package's theme.json still goes through this SAME load_theme()
// unchanged (dg::ThemePackage::load_theme_json(), theme_package.h) once its
// bytes are safely off disk. Path-traversal defence and the resource-tree
// bounds live in theme_package.h/.cpp, which is the one place "untrusted
// input" actually starts for this project.

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
  //
  // design.md section 12 open question 7 settled by 7-6: reject any
  // schema_version other than the exact set this build understands (today,
  // that set is {1}), never auto-upgrade. See doc/theme-packages.md.
  kUnsupportedSchemaVersion,

  // 7-6 (design.md section 5.7.5, external theme packages only - see
  // theme_package.h): theme.json, or a resource file it names, could not be
  // read at all (missing, not a regular file, a symlink loop, a permission
  // error).
  kIoError,

  // 7-6: a resource path resolved (after following any symlink) OUTSIDE the
  // theme package's own root directory - design.md section 5.7.5's
  // path-traversal restriction.
  kPathTraversal,

  // 7-6: a single file (theme.json itself, or one resource) exceeded its
  // size bound before any of its content was read.
  kResourceTooLarge,

  // 7-6: the resource tree as a whole exceeded a file-count or aggregate-size
  // bound - design.md's risk register names "解析炸弹" for theme.json; a
  // resource DIRECTORY has the identical shape of risk in a dimension a
  // single JSON document does not have (how many files, and how much do they
  // total), which this status is what catches.
  kTooManyResources,
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
