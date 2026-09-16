// External theme packages (design.md section 5.7.4/5.7.5, P7 slice 7-6).
//
// A theme package is a DIRECTORY a third party ships: theme.json plus a
// resource tree (fonts/icons/images). Everything under it is UNTRUSTED
// INPUT - design.md's own risk register names this explicitly ("第三方
// theme package 是不可信输入（路径穿越 / 解析炸弹 / 恶意 SVG）"), and this
// is the one file in the theme system where that risk is live rather than
// argued about: 6-2's `dg::load_theme()` correctly declined every one of
// section 5.7.5's four restrictions because the ONE theme it loaded was
// compiled into the binary, not third-party input - see theme_loader.h's
// own header comment. This file is what changes once a caller supplies a
// directory nobody at drawgui wrote.
//
// SCOPE DECISION (recorded here, not merely implied): resources are
// RASTER IMAGES ONLY, read as bytes for a caller to hand to
// dg::ImageCatalog::decode() (5-1's existing, unchanged decode path) - NOT
// SVG. design.md section 5.7.4 lists an `icons/` directory of SVG; 7-1 made
// `SkSVGDOM` a real, linkable capability this project's Skia distribution
// carries, but design.md section 5.7.5 names "恶意 SVG" as an explicit
// item in the SAME risk this file exists to defend against, and an SVG
// parser is a second, much larger attack surface (external references,
// unbounded path/node counts, its own recursive document structure) this
// slice does not have the scope to convergence-test the way mini_json.h's
// ~300 lines already are. Raster decoding reuses a decoder this project
// already fuzzed by construction (SkCodec, exercised since 5-1); SVG is
// declined BY NAME rather than half-built - see doc/theme-packages.md.
//
// Also NOT built: a font token type, and an ABI/schema type for "this
// token names an image" - see doc/theme-packages.md for why extending
// TokenType is a separate, larger decision (a new schema type ripples
// through the generator, the ABI value_type, and every consumer of
// TokenType's two-way switch) this slice does not make. What IS built is
// the one thing every one of those future features would still need: a
// path-traversal-safe way to read an arbitrary resource file's bytes out
// of a package, and a bounded scan of the resource tree as a whole.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "drawgui/base/expected.h"
#include "drawgui/theme/theme.h"
#include "drawgui/theme/theme_loader.h"

namespace dg {

// theme.json's own size bound - must not exceed mini_json::kMaxJsonBytes
// (src/theme/mini_json.h), checked via a stat() BEFORE any byte of the file
// is read, so an oversized file costs one syscall rather than an attempted
// allocation of its full (possibly enormous) size.
inline constexpr std::uintmax_t kMaxThemeJsonBytes = 1U << 20;  // 1 MiB

// A single resource file's size bound - independent of, and larger than,
// theme.json's own (an icon/image is expected to be bigger than a few KB of
// JSON, but still bounded: design.md's risk register names a decompression/
// parse bomb, and an unbounded single file is exactly that shape whether it
// is JSON or a bitmap SkCodec is asked to decode).
inline constexpr std::uintmax_t kMaxResourceFileBytes = 4U << 20;  // 4 MiB

// The resource tree AS A WHOLE has two bounds a single JSON document does
// not: how many files it contains, and how much they total. Neither bound
// is measured against a real caller's needs (no theme package this project
// ships has more than a handful of icons) - both are parse-bomb-shaped
// defensive limits, the same role kMaxJsonBytes/kMaxDepth already play for
// mini_json.h, sized generously enough that a legitimate package never
// approaches them.
inline constexpr std::size_t kMaxResourceFileCount = 256;
inline constexpr std::uintmax_t kMaxTotalResourceBytes = 32U << 20;  // 32 MiB

// One opened, validated theme package directory. Opening it canonicalizes
// the root and bounds-checks the resource tree (file count, per-file size,
// aggregate size - see the constants above) WITHOUT reading any resource's
// CONTENT, so a hostile package with an enormous file count or a single
// enormous file is rejected by open() itself, before a caller ever asks to
// read one.
class ThemePackage {
 public:
  // Canonicalizes `dir` (resolving symlinks and `..` the same way any
  // later resource read will) and bounds-checks the resource tree beneath
  // it. Fails with ThemeLoadStatus::kIoError if `dir` does not exist or is
  // not a directory, kTooManyResources if the tree's file count or
  // aggregate size exceeds the bounds above, or kResourceTooLarge if any
  // single file does.
  [[nodiscard]] static Expected<ThemePackage, ThemeLoadError> open(const std::string& dir);

  // Reads THIS package's theme.json (bounded by kMaxThemeJsonBytes, checked
  // before any byte is read) and fully validates it against
  // themes/schema.toml through the SAME dg::load_theme() the compiled-in
  // builtin theme already uses (theme_loader.h) - a package dogfoods the
  // identical loading path an embedded theme.json string already does,
  // design.md section 5.7.4's own "打包...加载接口不变" promise kept
  // literally for the directory case too.
  [[nodiscard]] Expected<Theme, ThemeLoadError> load_theme_json() const;

  // Reads `relative_path`'s bytes from within this package, refusing to
  // leave its root (design.md section 5.7.5's path-traversal restriction) -
  // the resource-loading primitive this slice builds. A caller decodes the
  // returned bytes itself (dg::ImageCatalog::decode() for a raster image);
  // this function knows nothing about image formats, only about safely
  // reaching a file's bytes.
  //
  // REJECTED, all as ThemeLoadStatus::kPathTraversal: an absolute path: a
  // path whose resolved (symlink-followed) form falls outside this
  // package's root, INCLUDING when a symlink INSIDE the package is what
  // points outside it - the check operates on the fully resolved path, not
  // on the literal string, so neither a raw `../` nor an indirection
  // through a symlink escapes it. A symlink LOOP is
  // ThemeLoadStatus::kIoError (it cannot be resolved to any real path at
  // all, so there is nothing to check "inside root or not" against) rather
  // than a crash or a hang.
  [[nodiscard]] Expected<std::vector<std::uint8_t>, ThemeLoadError> read_resource(
      std::string_view relative_path) const;

  [[nodiscard]] const std::string& root() const { return root_; }

 private:
  explicit ThemePackage(std::string root) : root_(std::move(root)) {}

  std::string root_;  // Always the CANONICALIZED absolute directory path.
};

// Hot reload (design.md section 5.7.6), in full: re-reads and re-validates
// `package`'s theme.json from disk, exactly as load_theme_json() did the
// first time. See doc/theme-packages.md for why this is the WHOLE
// mechanism - no tree rebuild, no incremental-patch machinery beyond what
// dg::ThemeBindings::apply() (6-2) already is, settling design.md section
// 12's hot-reload-granularity open question.
[[nodiscard]] Expected<Theme, ThemeLoadError> reload_theme_package(const ThemePackage& package);

}  // namespace dg
