// External theme packages (design.md section 5.7.4/5.7.5, P7 slice 7-6):
// path-traversal defence, resource-tree bounds, hot reload, and (7-6's own
// task instruction) a fuzz-style pass against the loader with hostile
// bytes, all as ThemeLoadError failures naming the exact reason - never a
// crash, never a hang, never a silent pass-through.
//
// Every case below uses a REAL filesystem directory under
// std::filesystem::temp_directory_path() rather than mocking the
// filesystem: design.md's own risk register names "路径穿越" for a REAL
// directory tree, and a mock cannot reproduce a real symlink loop's ELOOP
// or a real canonical-path resolution - the two things this file's
// strongest claims (traversal-via-symlink, symlink-loop-does-not-hang) rest
// on. Each test builds its own uniquely-named temp directory and removes it
// with a RAII guard, so a failing assertion never leaves stray files for
// the next run to trip over.

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/theme/theme.h"
#include "drawgui/theme/theme_loader.h"
#include "drawgui/theme/theme_package.h"

namespace {

namespace fs = std::filesystem;

using dg::ThemeLoadStatus;

// A fully valid package's theme.json - deliberately identical in shape to
// themes/builtin/theme.json (same 11 tokens, same "#RRGGBBAA" convention),
// so every test below is exercising the PACKAGE-LOADING machinery, not a
// second, ad hoc theme document.
constexpr const char* kValidThemeJson = R"({
  "schema_version": 1,
  "name": "test-package",
  "base": {
    "radius.sm": 2,
    "radius.md": 6,
    "space.sm": 3,
    "space.md": 7
  },
  "variants": {
    "light": {
      "color.surface": "#FFFFFFFF",
      "color.on-surface": "#111111FF",
      "color.border": "#AAAAAAFF",
      "color.primary": "#3366CCFF",
      "color.on-primary": "#FFFFFFFF",
      "color.primary-hover": "#4A78D6FF",
      "color.primary-pressed": "#2952A3FF",
      "color.focus-ring": "#FF8800FF"
    },
    "dark": {
      "color.surface": "#1E1E1EFF",
      "color.on-surface": "#E8E8E8FF",
      "color.border": "#3A3F45FF",
      "color.primary": "#6699FFFF",
      "color.on-primary": "#0D1117FF",
      "color.primary-hover": "#7FAAFFFF",
      "color.primary-pressed": "#5580D9FF",
      "color.focus-ring": "#FFA733FF"
    }
  }
})";

// One test's own temp directory, removed on scope exit regardless of how
// the test finishes (a REQUIRE failure throws in doctest, so a plain
// destructor - not a manual cleanup call at the end of the test body - is
// what makes this reliable).
class TempDir {
 public:
  explicit TempDir(const std::string& name) {
    path_ = fs::temp_directory_path() /
            ("drawgui_theme_package_test_" + name + "_" + std::to_string(::getpid()));
    fs::remove_all(path_);
    fs::create_directories(path_);
  }
  ~TempDir() { fs::remove_all(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  [[nodiscard]] const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

void write_file(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  file << content;
}

}  // namespace

TEST_CASE("ThemePackage::open loads a well-formed package, dogfooding the same load_theme()") {
  TempDir dir("valid");
  write_file(dir.path() / "theme.json", kValidThemeJson);

  const dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
      dg::ThemePackage::open(dir.path().string());
  REQUIRE(package.has_value());

  const dg::Expected<dg::Theme, dg::ThemeLoadError> theme = package.value().load_theme_json();
  REQUIRE(theme.has_value());
  CHECK(theme.value().int_value(9).value() == 6);  // radius.md
}

TEST_CASE("ThemePackage::open rejects a directory that does not exist") {
  const dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
      dg::ThemePackage::open("/nonexistent/drawgui/theme/package/path");
  REQUIRE_FALSE(package.has_value());
  CHECK(package.error().status == ThemeLoadStatus::kIoError);
}

TEST_CASE("read_resource: a legitimate nested resource path succeeds - the positive control") {
  TempDir dir("legit_resource");
  write_file(dir.path() / "theme.json", kValidThemeJson);
  write_file(dir.path() / "icons" / "ok.bin", "not a real image, just bytes");

  const dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
      dg::ThemePackage::open(dir.path().string());
  REQUIRE(package.has_value());

  const dg::Expected<std::vector<std::uint8_t>, dg::ThemeLoadError> bytes =
      package.value().read_resource("icons/ok.bin");
  REQUIRE(bytes.has_value());
  CHECK(bytes.value().size() == std::string("not a real image, just bytes").size());
}

TEST_CASE("read_resource: '../' traversal is rejected") {
  TempDir dir("traversal_dotdot");
  write_file(dir.path() / "theme.json", kValidThemeJson);
  write_file(dir.path().parent_path() / "secret.txt", "top secret");

  const dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
      dg::ThemePackage::open(dir.path().string());
  REQUIRE(package.has_value());

  const dg::Expected<std::vector<std::uint8_t>, dg::ThemeLoadError> bytes =
      package.value().read_resource("../secret.txt");
  REQUIRE_FALSE(bytes.has_value());
  CHECK(bytes.error().status == ThemeLoadStatus::kPathTraversal);

  fs::remove(dir.path().parent_path() / "secret.txt");
}

TEST_CASE("read_resource: an absolute path is rejected") {
  TempDir dir("traversal_absolute");
  write_file(dir.path() / "theme.json", kValidThemeJson);
  write_file(fs::temp_directory_path() / "drawgui_theme_absolute_secret.txt", "secret");

  const dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
      dg::ThemePackage::open(dir.path().string());
  REQUIRE(package.has_value());

  const std::string absolute =
      (fs::temp_directory_path() / "drawgui_theme_absolute_secret.txt").string();
  const dg::Expected<std::vector<std::uint8_t>, dg::ThemeLoadError> bytes =
      package.value().read_resource(absolute);
  REQUIRE_FALSE(bytes.has_value());
  CHECK(bytes.error().status == ThemeLoadStatus::kPathTraversal);

  fs::remove(fs::temp_directory_path() / "drawgui_theme_absolute_secret.txt");
}

TEST_CASE(
    "read_resource: a symlink INSIDE the package pointing OUTSIDE it is rejected - proves the "
    "check operates on the RESOLVED path, not the literal string") {
  TempDir dir("traversal_symlink");
  TempDir outside("traversal_symlink_target");
  write_file(dir.path() / "theme.json", kValidThemeJson);
  write_file(outside.path() / "secret.txt", "outside secret");
  fs::create_directory(dir.path() / "icons");
  fs::create_symlink(outside.path() / "secret.txt", dir.path() / "icons" / "evil.bin");

  const dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
      dg::ThemePackage::open(dir.path().string());
  REQUIRE(package.has_value());

  const dg::Expected<std::vector<std::uint8_t>, dg::ThemeLoadError> bytes =
      package.value().read_resource("icons/evil.bin");
  REQUIRE_FALSE(bytes.has_value());
  CHECK(bytes.error().status == ThemeLoadStatus::kPathTraversal);
}

TEST_CASE(
    "read_resource: a symlink LOOP is a clean error, not a hang or a crash - "
    "this is the case a naive string-only '../' scan cannot even express") {
  TempDir dir("symlink_loop");
  write_file(dir.path() / "theme.json", kValidThemeJson);
  fs::create_directory(dir.path() / "icons");
  fs::create_symlink(dir.path() / "icons" / "b", dir.path() / "icons" / "a");
  fs::create_symlink(dir.path() / "icons" / "a", dir.path() / "icons" / "b");

  const dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
      dg::ThemePackage::open(dir.path().string());
  REQUIRE(package.has_value());

  const dg::Expected<std::vector<std::uint8_t>, dg::ThemeLoadError> bytes =
      package.value().read_resource("icons/a");
  REQUIRE_FALSE(bytes.has_value());
  CHECK(bytes.error().status == ThemeLoadStatus::kIoError);
}

TEST_CASE(
    "DEFECT INJECTION: removing the is_within_root() guard would let '../' traversal succeed - "
    "this test is what would fail if that guard were removed, proving it is not provably inert") {
  // This case intentionally duplicates the '../' traversal test above under
  // a name that states the falsifiability claim directly, per this slice's
  // own task instruction ("make sure removing a traversal guard actually
  // fails a test"). The injection itself (commenting out the
  // is_within_root() check in theme_package.cpp) was run manually once,
  // confirmed this exact case starts returning bytes instead of
  // kPathTraversal, and was reverted - see doc/theme-packages.md section on
  // defect injection for the full record; this file only carries the
  // regression test that would have caught it.
  TempDir dir("injection_proof");
  write_file(dir.path() / "theme.json", kValidThemeJson);
  write_file(dir.path().parent_path() / "injection_secret.txt", "should never be readable");

  const dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
      dg::ThemePackage::open(dir.path().string());
  REQUIRE(package.has_value());
  const dg::Expected<std::vector<std::uint8_t>, dg::ThemeLoadError> bytes =
      package.value().read_resource("../injection_secret.txt");
  REQUIRE_FALSE(bytes.has_value());
  CHECK(bytes.error().status == ThemeLoadStatus::kPathTraversal);

  fs::remove(dir.path().parent_path() / "injection_secret.txt");
}

TEST_CASE("load_theme_json: an oversized theme.json is rejected before any of it is parsed") {
  TempDir dir("oversized_theme_json");
  std::string huge = R"({"schema_version":1,"name":")";
  huge.append(dg::kMaxThemeJsonBytes + 1024, 'x');
  huge += "\"}";
  write_file(dir.path() / "theme.json", huge);

  const dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
      dg::ThemePackage::open(dir.path().string());
  REQUIRE(package.has_value());

  const dg::Expected<dg::Theme, dg::ThemeLoadError> theme = package.value().load_theme_json();
  REQUIRE_FALSE(theme.has_value());
  CHECK(theme.error().status == ThemeLoadStatus::kResourceTooLarge);
}

TEST_CASE("load_theme_json: a deeply nested theme.json is rejected, re-verified through the "
          "package path rather than assumed still true from 6-2's own mini_json test") {
  TempDir dir("deep_nesting");
  std::string deep;
  for (int i = 0; i < 64; ++i) {
    deep += "[";
  }
  for (int i = 0; i < 64; ++i) {
    deep += "]";
  }
  const std::string json =
      std::string("{\"schema_version\":1,\"name\":\"deep\",\"base\":") + deep +
      ",\"variants\":{\"light\":{},\"dark\":{}}}";
  write_file(dir.path() / "theme.json", json);

  const dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
      dg::ThemePackage::open(dir.path().string());
  REQUIRE(package.has_value());

  const dg::Expected<dg::Theme, dg::ThemeLoadError> theme = package.value().load_theme_json();
  REQUIRE_FALSE(theme.has_value());
  CHECK(theme.error().status == ThemeLoadStatus::kParseError);
}

TEST_CASE("ThemePackage::open rejects a resource tree with too many files") {
  TempDir dir("too_many_files");
  write_file(dir.path() / "theme.json", kValidThemeJson);
  for (std::size_t i = 0; i < dg::kMaxResourceFileCount + 1; ++i) {
    write_file(dir.path() / "icons" / ("f" + std::to_string(i) + ".bin"), "x");
  }

  const dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
      dg::ThemePackage::open(dir.path().string());
  REQUIRE_FALSE(package.has_value());
  CHECK(package.error().status == ThemeLoadStatus::kTooManyResources);
}

TEST_CASE("ThemePackage::open rejects a single resource file over the per-file bound") {
  TempDir dir("oversized_resource");
  write_file(dir.path() / "theme.json", kValidThemeJson);
  {
    fs::create_directories(dir.path() / "images");
    std::ofstream file(dir.path() / "images" / "huge.bin", std::ios::binary);
    const std::string chunk(1 << 16, 'z');
    for (std::uintmax_t written = 0; written <= dg::kMaxResourceFileBytes;
        written += chunk.size()) {
      file << chunk;
    }
  }

  const dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
      dg::ThemePackage::open(dir.path().string());
  REQUIRE_FALSE(package.has_value());
  CHECK(package.error().status == ThemeLoadStatus::kResourceTooLarge);
}

TEST_CASE("reload_theme_package: editing theme.json on disk takes effect on the next call, with "
          "no widget tree or ThemePackage handle involved") {
  TempDir dir("reload");
  write_file(dir.path() / "theme.json", kValidThemeJson);

  const dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
      dg::ThemePackage::open(dir.path().string());
  REQUIRE(package.has_value());

  const dg::Expected<dg::Theme, dg::ThemeLoadError> first = package.value().load_theme_json();
  REQUIRE(first.has_value());
  CHECK(first.value().int_value(9).value() == 6);  // radius.md, as shipped above

  std::string edited = kValidThemeJson;
  const std::string needle = "\"radius.md\": 6";
  const std::size_t pos = edited.find(needle);
  REQUIRE(pos != std::string::npos);
  edited.replace(pos, needle.size(), "\"radius.md\": 99");
  write_file(dir.path() / "theme.json", edited);

  const dg::Expected<dg::Theme, dg::ThemeLoadError> reloaded =
      dg::reload_theme_package(package.value());
  REQUIRE(reloaded.has_value());
  CHECK(reloaded.value().int_value(9).value() == 99);
}

TEST_CASE("fuzz: random byte mutations of a valid theme.json never crash, hang, or throw - "
          "every result is an Expected, ok or a clean ThemeLoadError") {
  // A fixed seed, not a time-based one: a fuzz failure must be reproducible
  // by re-running this exact test, not only observable once in CI. 2000
  // iterations across a corpus of single-byte flips is small enough to run
  // in every CTest invocation (including under -DDG_SANITIZE=ON, where this
  // matters most - ASan/UBSan on a hand-rolled parser reading attacker-
  // shaped bytes is exactly this slice's own stated priority) rather than
  // needing a separate, slower fuzz target.
  std::mt19937 rng(0xD6A57E4Du);
  const std::string original = kValidThemeJson;

  TempDir dir("fuzz");
  for (int iteration = 0; iteration < 2000; ++iteration) {
    std::string mutated = original;
    const int mutations = 1 + static_cast<int>(rng() % 6);
    for (int m = 0; m < mutations; ++m) {
      const std::size_t at = rng() % mutated.size();
      mutated[at] = static_cast<char>(rng() % 256);
    }
    write_file(dir.path() / "theme.json", mutated);

    const dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
        dg::ThemePackage::open(dir.path().string());
    REQUIRE(package.has_value());
    // The point under test: this call must return, not hang, and must not
    // throw past dg::Expected's boundary - whichever it is, ok or an error,
    // is an acceptable outcome for RANDOM bytes; only a crash/hang/throw
    // would fail this loop.
    const dg::Expected<dg::Theme, dg::ThemeLoadError> result = package.value().load_theme_json();
    (void)result;
  }
}
