#include "drawgui/render/font_catalog.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_directory.h"

#include "render/font_access.h"
#include "render/font_catalog_impl.h"

namespace dg {
namespace {

constexpr SkFontTableTag kCbdt = SkSetFourByteTag('C', 'B', 'D', 'T');
constexpr SkFontTableTag kSbix = SkSetFourByteTag('s', 'b', 'i', 'x');
constexpr SkFontTableTag kColr = SkSetFourByteTag('C', 'O', 'L', 'R');

char lower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Builds one representative face per family and sorts the result by family
// name. See FontCatalog::scan in the header for why sorting is required.
std::vector<FaceEntry> build_pool(const SkFontMgr& manager) {
  std::vector<FaceEntry> pool;
  const int families = manager.countFamilies();
  pool.reserve(static_cast<std::size_t>(families));
  for (int index = 0; index < families; ++index) {
    SkString name;
    manager.getFamilyName(index, &name);
    sk_sp<SkFontStyleSet> styles = manager.createStyleSet(index);
    if (!styles || styles->count() == 0) {
      continue;
    }
    sk_sp<SkTypeface> face = styles->matchStyle(SkFontStyle::Normal());
    if (!face) {
      continue;
    }
    const bool colour = has_colour_glyphs(*face);
    pool.push_back(FaceEntry{std::string{name.c_str()}, std::move(face), colour});
  }
  std::sort(pool.begin(), pool.end(),
            [](const FaceEntry& a, const FaceEntry& b) { return a.family < b.family; });
  return pool;
}

// Most specific first, so a "zh-Hans" rule beats a "zh" rule beats the generic
// one whatever order the caller listed them in. Stable, so two rules of equal
// specificity keep the caller's order and the answer stays a function of the
// table rather than of the sort.
void order_by_specificity(std::vector<FontFallbackRule>& rules) {
  std::stable_sort(rules.begin(), rules.end(),
                   [](const FontFallbackRule& a, const FontFallbackRule& b) {
                     return a.language.size() > b.language.size();
                   });
}

}  // namespace

bool has_colour_glyphs(const SkTypeface& face) {
  return face.getTableSize(kCbdt) > 0 || face.getTableSize(kSbix) > 0 ||
         face.getTableSize(kColr) > 0;
}

bool language_matches(std::string_view rule, std::string_view tag) {
  if (rule.empty()) {
    return true;
  }
  if (tag.size() < rule.size()) {
    return false;
  }
  for (std::size_t i = 0; i < rule.size(); ++i) {
    if (lower(rule[i]) != lower(tag[i])) {
      return false;
    }
  }
  return tag.size() == rule.size() || tag[rule.size()] == '-';
}

bool prefers_colour_font(char32_t codepoint) {
  return codepoint >= 0x1F000 && codepoint <= 0x1FAFF;
}

FontCatalog::FontCatalog(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

Expected<FontCatalog, FontError> FontCatalog::scan(const std::string& directory) {
  auto impl = std::make_shared<Impl>();
  impl->manager = SkFontMgr_New_Custom_Directory(directory.c_str());
  if (!impl->manager) {
    return Unexpected{FontError{"no font manager could be built from " + directory}};
  }
  if (impl->manager->countFamilies() == 0) {
    return Unexpected{FontError{"no font families were found under " + directory}};
  }
  impl->chain.pool = build_pool(*impl->manager);
  impl->chain.rules = default_fallback_rules();
  order_by_specificity(impl->chain.rules);
  return FontCatalog{std::move(impl)};
}

Expected<FontId, FontError> FontCatalog::add(const std::string& family, bool bold) {
  const SkFontStyle style = bold ? SkFontStyle::Bold() : SkFontStyle::Normal();
  sk_sp<SkTypeface> typeface = impl_->manager->matchFamilyStyle(family.c_str(), style);
  if (!typeface) {
    return Unexpected{FontError{"no font family named '" + family + "'; this machine offers " +
                                available_families()}};
  }

  // The table is append-only and the ids are one-based, so the id of the entry
  // about to be pushed is the size before the push plus one. Zero stays
  // reserved for "no font".
  impl_->typefaces.push_back(std::move(typeface));
  return FontId{static_cast<std::uint16_t>(impl_->typefaces.size())};
}

std::size_t FontCatalog::size() const {
  return impl_->typefaces.size();
}

bool FontCatalog::holds(FontId id) const {
  return id.is_valid() && id.value <= impl_->typefaces.size();
}

std::string FontCatalog::family_name(FontId id) const {
  sk_sp<SkTypeface> face = FontAccess::typeface(*this, id);
  if (!face) {
    return {};
  }
  SkString name;
  face->getFamilyName(&name);
  return std::string{name.c_str()};
}

std::string FontCatalog::available_families() const {
  std::string names;
  for (const FaceEntry& entry : impl_->chain.pool) {
    if (!names.empty()) {
      names += ", ";
    }
    names += entry.family;
  }
  return names;
}

void FontCatalog::set_fallback_rules(std::vector<FontFallbackRule> rules) {
  impl_->chain.rules = std::move(rules);
  order_by_specificity(impl_->chain.rules);
}

std::vector<std::string> FontCatalog::fallback_families() const {
  std::vector<std::string> names;
  names.reserve(impl_->chain.pool.size());
  for (const FaceEntry& entry : impl_->chain.pool) {
    names.push_back(entry.family);
  }
  return names;
}

// The shipped chain (design.md section 5.13.5).
//
// It is a TABLE, not an algorithm, and that is the point: per-language font
// preference cannot be derived from the fonts themselves. Measured on this
// machine - every CJK font present declares ALL FIVE CJK codepages in its OS/2
// ulCodePageRange (932 Japanese, 936 simplified, 949 and 1361 Korean, 950
// traditional), so the font's own metadata cannot tell Japanese from Chinese.
// fontconfig does not derive it either; it reads /etc/fonts. The choice is
// therefore between a table this repository owns and a table the host owns,
// and only the first has an answer that is the same on two machines.
//
// Families this machine does not have are skipped, so one table names the
// right font on Linux, Windows and macOS without any conditional compilation.
std::vector<FontFallbackRule> FontCatalog::default_fallback_rules() {
  // Kept as named locals so the shared tails are visible rather than
  // accidentally divergent copies.
  const std::vector<std::string> hans = {
      "Noto Sans CJK SC",  "Source Han Sans SC",  "Noto Sans SC",
      "PingFang SC",       "Microsoft YaHei",     "SimSun",
      "WenQuanYi Zen Hei", "WenQuanYi Micro Hei", "Droid Sans Fallback"};
  const std::vector<std::string> hant = {
      "Noto Sans CJK TC",  "Source Han Sans TC",  "Noto Sans TC",
      "PingFang TC",       "Microsoft JhengHei",  "PMingLiU",
      "WenQuanYi Zen Hei", "WenQuanYi Micro Hei", "Droid Sans Fallback"};
  const std::vector<std::string> japanese = {
      "Noto Sans CJK JP", "Source Han Sans JP", "Noto Sans JP", "Hiragino Sans", "Yu Gothic",
      "Meiryo", "IPAGothic", "TakaoPGothic", "Droid Sans Japanese",
      // Last, and knowingly wrong: a Chinese face draws Japanese Han with
      // Chinese glyph shapes. It is still preferred over a tofu box, and
      // doc/font-fallback.md says so out loud rather than letting a reader
      // believe the ja chain succeeded.
      "WenQuanYi Zen Hei", "Droid Sans Fallback"};
  const std::vector<std::string> korean = {
      "Noto Sans CJK KR",   "Source Han Sans K", "Noto Sans KR", "Apple SD Gothic Neo",
      "Malgun Gothic",      "NanumGothic",       "UnDotum",      "WenQuanYi Zen Hei",
      "Droid Sans Fallback"};

  return {
      {"zh-Hans", hans},
      {"zh-CN", hans},
      {"zh-SG", hans},
      {"zh-Hant", hant},
      {"zh-TW", hant},
      {"zh-HK", hant},
      {"zh", hans},
      {"ja", japanese},
      {"ko", korean},
      // The generic chain. Latin first because most text is Latin and the
      // first face that covers a codepoint wins, then the broad-coverage
      // families, then CJK, then colour emoji.
      {"",
       {"Noto Sans", "DejaVu Sans", "Ubuntu Sans", "Noto Sans Symbols 2", "Noto Sans Symbols",
        "Noto Sans CJK SC", "WenQuanYi Zen Hei", "Droid Sans Fallback", "Noto Color Emoji",
        "Segoe UI Emoji", "Apple Color Emoji"}},
  };
}

sk_sp<SkTypeface> FontAccess::typeface(const FontCatalog& catalog, FontId id) {
  if (!catalog.holds(id)) {
    return nullptr;
  }
  return catalog.impl_->typefaces[id.value - 1];
}

sk_sp<SkFontMgr> FontAccess::font_manager(const FontCatalog& catalog) {
  return catalog.impl_->manager;
}

}  // namespace dg
