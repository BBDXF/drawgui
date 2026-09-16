// See theme_package.h for the design; this is the implementation of
// design.md section 5.7.5's restrictions - path traversal, size bounds,
// resource-tree bounds - against untrusted directory input.

#include "drawgui/theme/theme_package.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace dg {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] Unexpected<ThemeLoadError> fail(ThemeLoadStatus status, std::string message) {
  return Unexpected(ThemeLoadError{status, std::move(message)});
}

// True when `path` (already resolved - no symlinks, no `..` left in it) is
// `root` itself or a descendant of it. Both arguments must already be
// canonical; this is a pure string-prefix check on paths libc has already
// resolved, not a substring scan of the ORIGINAL (unresolved) input - the
// distinction is what makes it safe against `../` and against a symlink
// indirection alike, since either one is already gone by the time this
// runs.
[[nodiscard]] bool is_within_root(const fs::path& root, const fs::path& resolved) {
  if (resolved == root) {
    return true;
  }
  auto root_it = root.begin();
  auto resolved_it = resolved.begin();
  for (; root_it != root.end(); ++root_it, ++resolved_it) {
    if (resolved_it == resolved.end() || *resolved_it != *root_it) {
      return false;
    }
  }
  return true;
}

// A bounded, non-content-reading walk of `root`'s resource tree: counts
// files and sums declared sizes via stat() only, bailing out the moment
// either bound in theme_package.h is exceeded rather than finishing the
// enumeration first. recursive_directory_iterator does not follow a
// directory symlink by default (no follow_directory_symlink option is
// passed below), which is what keeps a symlink LOOP inside the resource
// tree from being an infinite walk in the first place - the individual
// read_resource() call is what catches a symlink escaping the root, this
// scan's job is only the two tree-shaped bounds.
[[nodiscard]] Expected<void, ThemeLoadError> scan_bounded(const fs::path& root) {
  std::error_code ec;
  std::size_t file_count = 0;
  std::uintmax_t total_bytes = 0;
  fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
  if (ec) {
    return fail(ThemeLoadStatus::kIoError, root.string() + ": " + ec.message());
  }
  const fs::recursive_directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) {
      return fail(ThemeLoadStatus::kIoError, root.string() + ": " + ec.message());
    }
    if (!it->is_regular_file(ec)) {
      continue;
    }
    ++file_count;
    if (file_count > kMaxResourceFileCount) {
      return fail(ThemeLoadStatus::kTooManyResources,
                  root.string() + ": more than " + std::to_string(kMaxResourceFileCount) +
                      " resource files");
    }
    const std::uintmax_t size = it->file_size(ec);
    if (ec) {
      continue;
    }
    if (size > kMaxResourceFileBytes) {
      return fail(ThemeLoadStatus::kResourceTooLarge,
                  it->path().string() + ": exceeds " + std::to_string(kMaxResourceFileBytes) +
                      " bytes");
    }
    total_bytes += size;
    if (total_bytes > kMaxTotalResourceBytes) {
      return fail(ThemeLoadStatus::kTooManyResources,
                  root.string() + ": resource tree exceeds " +
                      std::to_string(kMaxTotalResourceBytes) + " bytes in total");
    }
  }
  return Expected<void, ThemeLoadError>{};
}

[[nodiscard]] Expected<std::vector<std::uint8_t>, ThemeLoadError> read_bounded_file(
    const fs::path& path, std::uintmax_t max_bytes) {
  std::error_code ec;
  if (!fs::is_regular_file(path, ec) || ec) {
    return fail(ThemeLoadStatus::kIoError, path.string() + ": not a regular file");
  }
  const std::uintmax_t size = fs::file_size(path, ec);
  if (ec) {
    return fail(ThemeLoadStatus::kIoError, path.string() + ": " + ec.message());
  }
  if (size > max_bytes) {
    return fail(ThemeLoadStatus::kResourceTooLarge,
                path.string() + ": exceeds " + std::to_string(max_bytes) + " bytes");
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return fail(ThemeLoadStatus::kIoError, path.string() + ": could not open");
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  if (size > 0) {
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!file) {
      return fail(ThemeLoadStatus::kIoError, path.string() + ": short read");
    }
  }
  return bytes;
}

}  // namespace

Expected<ThemePackage, ThemeLoadError> ThemePackage::open(const std::string& dir) {
  std::error_code ec;
  const fs::path canonical_root = fs::canonical(dir, ec);
  if (ec) {
    return fail(ThemeLoadStatus::kIoError, dir + ": " + ec.message());
  }
  if (!fs::is_directory(canonical_root, ec) || ec) {
    return fail(ThemeLoadStatus::kIoError, dir + ": not a directory");
  }

  Expected<void, ThemeLoadError> scanned = scan_bounded(canonical_root);
  if (!scanned) {
    return Unexpected(scanned.error());
  }

  return ThemePackage(canonical_root.string());
}

Expected<Theme, ThemeLoadError> ThemePackage::load_theme_json() const {
  const fs::path json_path = fs::path(root_) / "theme.json";
  Expected<std::vector<std::uint8_t>, ThemeLoadError> bytes =
      read_bounded_file(json_path, kMaxThemeJsonBytes);
  if (!bytes) {
    return Unexpected(bytes.error());
  }
  const std::string_view text(reinterpret_cast<const char*>(bytes.value().data()),
                              bytes.value().size());
  return load_theme(text);
}

Expected<std::vector<std::uint8_t>, ThemeLoadError> ThemePackage::read_resource(
    std::string_view relative_path) const {
  if (relative_path.empty()) {
    return fail(ThemeLoadStatus::kPathTraversal, "empty resource path");
  }
  const fs::path relative(relative_path);
  if (relative.is_absolute()) {
    return fail(ThemeLoadStatus::kPathTraversal,
                std::string(relative_path) + ": absolute paths are not allowed");
  }

  const fs::path root_path(root_);
  std::error_code ec;
  const fs::path resolved = fs::canonical(root_path / relative, ec);
  if (ec) {
    // Covers "does not exist" and a symlink LOOP alike (ELOOP surfaces
    // through the same error_code channel) - neither has a resolved path to
    // check against the root, so neither can be a traversal finding either;
    // both are simply "could not read this resource".
    return fail(ThemeLoadStatus::kIoError, std::string(relative_path) + ": " + ec.message());
  }
  if (!is_within_root(root_path, resolved)) {
    return fail(ThemeLoadStatus::kPathTraversal,
                std::string(relative_path) + ": resolves outside the theme package root");
  }
  return read_bounded_file(resolved, kMaxResourceFileBytes);
}

Expected<Theme, ThemeLoadError> reload_theme_package(const ThemePackage& package) {
  return package.load_theme_json();
}

}  // namespace dg
