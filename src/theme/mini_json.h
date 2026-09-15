// A minimal JSON parser, scoped exactly to what theme.json needs.
//
// SEE doc/theme.md for the decision this settles (design.md section 12
// open question 3, "JSON 解析器选型... P3 前定案"): drawgui has pulled in
// exactly three third-party dependencies in its whole history (Skia, SDL3,
// FreeType - and doctest for tests only), and theme.json's own grammar is a
// closed, two-level, fully schema-validated subset: an object with a number,
// a string, one flat object of string->number ("base"), and one object of
// objects of string->string ("variants.<name>"). No arrays, no booleans, no
// null, and nesting never exceeds three levels by construction. That is a
// small enough grammar that a purpose-built parser is both correct and
// auditable in about 200 lines - not a subset that happens to be convenient
// to hand-roll, a subset chosen because THIS is what theme.json actually
// needs (icons/fonts/images arrays are named out of this slice's scope,
// doc/theme.md section on scope).
//
// This file still parses the FULL JSON GRAMMAR (objects, arrays, strings,
// numbers, true/false/null) rather than only the theme.json subset, on
// purpose: theme_loader.cpp's OWN validation (against themes/schema.toml)
// is what rejects the wrong shape with a location-carrying error, and that
// validation is a more useful and more precisely-located error than a
// parser that already assumes the shape and fails confusingly on anything
// else. The parser's only two defensive limits are depth and size - see
// kMaxDepth/kMaxJsonBytes below - which exist because "a JSON parser
// consuming untrusted-shaped input is exactly ASan/UBSan's domain"
// (this slice's own task) and design.md's risk register (section 10) names
// "解析炸弹" (a parse bomb) by name for theme loading; a parser with no
// recursion-depth bound is exactly the shape a parse bomb exploits, doubly
// so for one this project just wrote itself rather than pulled from a
// hardened, widely-fuzzed library. Bounding depth explicitly is what makes
// "no third-party JSON parser" a defensible trade rather than a shortcut.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "drawgui/base/expected.h"

namespace dg::mini_json {

// A byte offset into the source text, carried into every error so a
// diagnostic can name where parsing failed - design.md section 5.7.5's
// "unknown token name -> ... report out the specific location" applies to
// the PARSE stage too, not only the schema-validation stage above it.
struct ParseError {
  std::size_t offset = 0;
  std::string message;
};

class Value;
using Array = std::vector<Value>;
// Preserves insertion order and allows lookup by key without a second
// index - theme.json objects are small (a dozen keys at most), so linear
// scan is both simpler and cheaper here than a hash map would be.
using Object = std::vector<std::pair<std::string, Value>>;

enum class Kind : std::uint8_t {
  kNull,
  kBool,
  kNumber,
  kString,
  kArray,
  kObject,
};

class Value {
 public:
  Value() : storage_(nullptr) {}
  explicit Value(bool value) : storage_(value) {}
  explicit Value(double value) : storage_(value) {}
  explicit Value(std::string value) : storage_(std::move(value)) {}
  explicit Value(Array value) : storage_(std::move(value)) {}
  explicit Value(Object value) : storage_(std::move(value)) {}

  [[nodiscard]] Kind kind() const {
    switch (storage_.index()) {
      case 0:
        return Kind::kNull;
      case 1:
        return Kind::kBool;
      case 2:
        return Kind::kNumber;
      case 3:
        return Kind::kString;
      case 4:
        return Kind::kArray;
      default:
        return Kind::kObject;
    }
  }

  [[nodiscard]] const double* as_number() const { return std::get_if<double>(&storage_); }
  [[nodiscard]] const std::string* as_string() const {
    return std::get_if<std::string>(&storage_);
  }
  [[nodiscard]] const Object* as_object() const { return std::get_if<Object>(&storage_); }
  [[nodiscard]] const Array* as_array() const { return std::get_if<Array>(&storage_); }

  // Linear lookup, adequate for theme.json's small objects (see Object's
  // own comment). Returns nullptr when this is not an object or the key is
  // absent, so a caller need not check kind() first.
  [[nodiscard]] const Value* find(std::string_view key) const {
    const Object* obj = as_object();
    if (obj == nullptr) {
      return nullptr;
    }
    for (const auto& [entry_key, entry_value] : *obj) {
      if (entry_key == key) {
        return &entry_value;
      }
    }
    return nullptr;
  }

 private:
  std::variant<std::nullptr_t, bool, double, std::string, Array, Object> storage_;
};

// A parse bomb defence, not a measured limit: theme.json is a few KB of
// flat key-value pairs, and a well-formed instance never comes close to
// either bound. kMaxDepth caps recursive descent so a maliciously (or
// accidentally) deep input fails with a clean ParseError rather than
// exhausting the call stack; kMaxJsonBytes caps the whole input so an
// enormous file is rejected before any parsing work happens at all.
inline constexpr int kMaxDepth = 32;
inline constexpr std::size_t kMaxJsonBytes = 1 << 20;  // 1 MiB

[[nodiscard]] Expected<Value, ParseError> parse(std::string_view text);

}  // namespace dg::mini_json
