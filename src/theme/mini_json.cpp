#include "mini_json.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace dg::mini_json {
namespace {

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  [[nodiscard]] Expected<Value, ParseError> run() {
    if (text_.size() > kMaxJsonBytes) {
      return Unexpected(
          ParseError{0, "input exceeds the " + std::to_string(kMaxJsonBytes) + " byte limit"});
    }
    skip_ws();
    Expected<Value, ParseError> value = parse_value(0);
    if (!value) {
      return value;
    }
    skip_ws();
    if (pos_ != text_.size()) {
      return fail("trailing data after the top-level value");
    }
    return value;
  }

 private:
  [[nodiscard]] Unexpected<ParseError> fail(std::string message) const {
    return Unexpected(ParseError{pos_, std::move(message)});
  }

  void skip_ws() {
    while (pos_ < text_.size()) {
      const char ch = text_[pos_];
      if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  [[nodiscard]] bool at_end() const { return pos_ >= text_.size(); }
  [[nodiscard]] char peek() const { return text_[pos_]; }

  [[nodiscard]] bool consume_literal(std::string_view literal) {
    if (text_.substr(pos_, literal.size()) != literal) {
      return false;
    }
    pos_ += literal.size();
    return true;
  }

  [[nodiscard]] Expected<Value, ParseError> parse_value(int depth) {
    if (depth > kMaxDepth) {
      return fail("exceeds the maximum nesting depth (" + std::to_string(kMaxDepth) + ")");
    }
    skip_ws();
    if (at_end()) {
      return fail("unexpected end of input, expected a value");
    }
    switch (peek()) {
      case '{':
        return parse_object(depth);
      case '[':
        return parse_array(depth);
      case '"':
        return parse_string_value();
      case 't':
        if (consume_literal("true")) {
          return Value{true};
        }
        return fail("invalid literal, expected 'true'");
      case 'f':
        if (consume_literal("false")) {
          return Value{false};
        }
        return fail("invalid literal, expected 'false'");
      case 'n':
        if (consume_literal("null")) {
          return Value{};
        }
        return fail("invalid literal, expected 'null'");
      default:
        return parse_number();
    }
  }

  // Encodes a \uXXXX escape's four hex digits into UTF-8, appended to
  // `out`. Split out of parse_string_raw() to keep its own cognitive
  // complexity under the configured clang-tidy threshold - this helper has
  // no branch parse_string_raw() itself needs to see.
  [[nodiscard]] std::optional<ParseError> parse_unicode_escape(std::string& out) {
    if (pos_ + 4 > text_.size()) {
      return ParseError{pos_, "truncated \\u escape"};
    }
    std::uint32_t code = 0;
    for (int i = 0; i < 4; ++i) {
      const char hex = text_[pos_ + static_cast<std::size_t>(i)];
      code <<= 4;
      if (hex >= '0' && hex <= '9') {
        code |= static_cast<std::uint32_t>(hex - '0');
      } else if (hex >= 'a' && hex <= 'f') {
        code |= static_cast<std::uint32_t>(hex - 'a' + 10);
      } else if (hex >= 'A' && hex <= 'F') {
        code |= static_cast<std::uint32_t>(hex - 'A' + 10);
      } else {
        return ParseError{pos_, "invalid \\u escape digit"};
      }
    }
    pos_ += 4;
    // theme.json needs no codepoint outside the Basic Multilingual Plane
    // (token names and colour strings are ASCII); a bare UTF-8 encode of
    // the BMP value is enough and avoids a surrogate-pair combiner this
    // grammar never has cause to exercise.
    if (code < 0x80) {
      out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (code >> 6)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (code >> 12)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
    return std::nullopt;
  }

  // One escape character (the byte right after the backslash), appended to
  // `out`. Returns an error for anything JSON does not define.
  [[nodiscard]] std::optional<ParseError> parse_escape(char esc, std::string& out) {
    switch (esc) {
      case '"':
        out.push_back('"');
        return std::nullopt;
      case '\\':
        out.push_back('\\');
        return std::nullopt;
      case '/':
        out.push_back('/');
        return std::nullopt;
      case 'b':
        out.push_back('\b');
        return std::nullopt;
      case 'f':
        out.push_back('\f');
        return std::nullopt;
      case 'n':
        out.push_back('\n');
        return std::nullopt;
      case 'r':
        out.push_back('\r');
        return std::nullopt;
      case 't':
        out.push_back('\t');
        return std::nullopt;
      case 'u':
        return parse_unicode_escape(out);
      default:
        return ParseError{pos_, "invalid escape character"};
    }
  }

  [[nodiscard]] Expected<std::string, ParseError> parse_string_raw() {
    if (at_end() || peek() != '"') {
      return Unexpected(ParseError{pos_, "expected a string"});
    }
    ++pos_;  // opening quote
    std::string out;
    while (true) {
      if (at_end()) {
        return Unexpected(ParseError{pos_, "unterminated string"});
      }
      const char ch = text_[pos_];
      if (ch == '"') {
        ++pos_;
        return out;
      }
      if (static_cast<unsigned char>(ch) < 0x20) {
        return Unexpected(ParseError{pos_, "control character in string"});
      }
      if (ch != '\\') {
        out.push_back(ch);
        ++pos_;
        continue;
      }
      ++pos_;  // backslash
      if (at_end()) {
        return Unexpected(ParseError{pos_, "unterminated escape sequence"});
      }
      const char esc = text_[pos_++];
      const std::optional<ParseError> error = parse_escape(esc, out);
      if (error.has_value()) {
        return Unexpected(*error);
      }
    }
  }

  [[nodiscard]] Expected<Value, ParseError> parse_string_value() {
    Expected<std::string, ParseError> str = parse_string_raw();
    if (!str) {
      return Unexpected(str.error());
    }
    return Value{std::move(str.value())};
  }

  [[nodiscard]] bool at_digit() const {
    return !at_end() && std::isdigit(static_cast<unsigned char>(peek())) != 0;
  }

  // How many digits were consumed - callers that require at least one turn
  // a zero result into their own error message.
  std::size_t consume_digits() {
    const std::size_t start = pos_;
    while (at_digit()) {
      ++pos_;
    }
    return pos_ - start;
  }

  [[nodiscard]] Expected<Value, ParseError> parse_number() {
    const std::size_t start = pos_;
    if (!at_end() && peek() == '-') {
      ++pos_;
    }
    if (consume_digits() == 0) {
      return fail("invalid number");
    }
    if (!at_end() && peek() == '.') {
      ++pos_;
      if (consume_digits() == 0) {
        return fail("invalid number: digits required after '.'");
      }
    }
    if (!at_end() && (peek() == 'e' || peek() == 'E')) {
      ++pos_;
      if (!at_end() && (peek() == '+' || peek() == '-')) {
        ++pos_;
      }
      if (consume_digits() == 0) {
        return fail("invalid number: digits required in exponent");
      }
    }
    const std::string token(text_.substr(start, pos_ - start));
    try {
      return Value{std::stod(token)};
    } catch (const std::exception&) {
      return Unexpected(ParseError{start, "number out of range"});
    }
  }

  [[nodiscard]] Expected<Value, ParseError> parse_array(int depth) {
    ++pos_;  // '['
    Array items;
    skip_ws();
    if (!at_end() && peek() == ']') {
      ++pos_;
      return Value{std::move(items)};
    }
    while (true) {
      Expected<Value, ParseError> item = parse_value(depth + 1);
      if (!item) {
        return item;
      }
      items.push_back(std::move(item.value()));
      skip_ws();
      if (at_end()) {
        return fail("unterminated array");
      }
      if (peek() == ',') {
        ++pos_;
        continue;
      }
      if (peek() == ']') {
        ++pos_;
        return Value{std::move(items)};
      }
      return fail("expected ',' or ']' in array");
    }
  }

  [[nodiscard]] Expected<Value, ParseError> parse_object(int depth) {
    ++pos_;  // '{'
    Object members;
    skip_ws();
    if (!at_end() && peek() == '}') {
      ++pos_;
      return Value{std::move(members)};
    }
    while (true) {
      skip_ws();
      Expected<std::string, ParseError> key = parse_string_raw();
      if (!key) {
        return Unexpected(key.error());
      }
      skip_ws();
      if (at_end() || peek() != ':') {
        return fail("expected ':' after object key");
      }
      ++pos_;
      Expected<Value, ParseError> value = parse_value(depth + 1);
      if (!value) {
        return value;
      }
      members.emplace_back(std::move(key.value()), std::move(value.value()));
      skip_ws();
      if (at_end()) {
        return fail("unterminated object");
      }
      if (peek() == ',') {
        ++pos_;
        continue;
      }
      if (peek() == '}') {
        ++pos_;
        return Value{std::move(members)};
      }
      return fail("expected ',' or '}' in object");
    }
  }

  std::string_view text_;
  std::size_t pos_ = 0;
};

}  // namespace

Expected<Value, ParseError> parse(std::string_view text) {
  Parser parser(text);
  return parser.run();
}

}  // namespace dg::mini_json
