#include "drawgui/render/font_catalog.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_directory.h"

#include "render/font_access.h"

namespace dg {

struct FontCatalog::Impl {
  sk_sp<SkFontMgr> manager;
  std::vector<sk_sp<SkTypeface>> typefaces;
};

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

std::string FontCatalog::available_families() const {
  std::string names;
  const int count = impl_->manager->countFamilies();
  for (int index = 0; index < count; ++index) {
    SkString name;
    impl_->manager->getFamilyName(index, &name);
    if (!names.empty()) {
      names += ", ";
    }
    names += name.c_str();
  }
  return names;
}

sk_sp<SkTypeface> FontAccess::typeface(const FontCatalog& catalog, FontId id) {
  if (!catalog.holds(id)) {
    return nullptr;
  }
  return catalog.impl_->typefaces[id.value - 1];
}

}  // namespace dg
