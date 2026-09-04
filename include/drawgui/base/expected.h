// dg::Expected<T, E> - a value, or the reason there is not one.
//
// design.md section 3.1 requires this. The C++23 library features
// (std::expected, std::print, std::mdspan) are deliberately not used: the
// language baseline is C++20 because Android NDK r27 and Xcode 15 libc++
// implement C++20 completely while their C++23 *library* coverage is uneven.
// Reaching for std::expected here would make the mobile port fail at the
// worst possible moment, so this is the minimal in-house replacement that
// section names.
//
// Scope is deliberately small - only what this project consumes. There is no
// monadic and_then / or_else / transform, no converting construction between
// different Expected instantiations, no std::unexpected interop, and no
// operator* / operator->. Each of those is API surface that would have to keep
// working on five platforms; they can be added when a caller needs them.
//
// Storage is std::variant, which is C++17 and therefore inside the baseline.
// A hand-rolled union is the obvious alternative and was rejected: the C++
// Core Guidelines forbid naked unions (Type.7) and clang-tidy enforces that
// here through cppcoreguidelines-pro-type-union-access. std::variant also
// gets the hard parts right without this file having to - it destroys the
// active alternative, and its special member functions stay trivial when both
// alternatives are trivial, so Expected<int, ErrorCode> is still trivially
// copyable and trivially destructible. Nothing in this header allocates.

#pragma once

#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>
#include <variant>

namespace dg {

namespace detail {

inline constexpr const char* kExpectedNoValue = "value() called on an error state";
inline constexpr const char* kExpectedNoError = "error() called on a value state";

// Reports a broken precondition and terminates.
//
// design.md section 5.17.5 asks a debug build to abort with diagnostics and a
// release build to degrade to recoverable behaviour, never silently. For
// value() on an error state there is no recoverable degradation to offer: the
// only thing this function could return is a fabricated value, and a
// fabricated surface or size then travels into the render tree and fails
// somewhere unrelated, with the real cause long gone. So both configurations
// report and abort, and value_or() is the recoverable accessor - it has no
// precondition at all.
//
// This function is not constexpr on purpose. A misuse inside a constant
// expression is therefore a compile error rather than a runtime abort.
//
// stderr is the channel only because the host log callback of section 5.17.5
// does not exist yet. This is the abort path, not logging.
[[noreturn]] inline void expected_contract_violation(const char* what) {
  std::fputs("drawgui: Expected precondition violated: ", stderr);
  std::fputs(what, stderr);
  std::fputs("\n", stderr);
  std::fflush(stderr);
  std::abort();
}

}  // namespace detail

// The error side of an Expected, spelled out at the construction site.
//
// Without this wrapper Expected<int, int> could not tell a value of 5 from an
// error of 5. Requiring the error side to name itself makes that case
// unambiguous, and makes every error construction visible when reading a
// return statement.
template <typename E>
class Unexpected {
 public:
  static_assert(!std::is_reference_v<E>, "Unexpected<E> stores E by value");

  // An error with no error in it is not a thing this type can represent.
  Unexpected() = delete;

  constexpr explicit Unexpected(const E& error) : error_(error) {}
  constexpr explicit Unexpected(E&& error) : error_(std::move(error)) {}

  [[nodiscard]] constexpr const E& error() const& noexcept { return error_; }
  [[nodiscard]] constexpr E& error() & noexcept { return error_; }
  [[nodiscard]] constexpr E&& error() && noexcept { return std::move(error_); }

  friend constexpr bool operator==(const Unexpected&, const Unexpected&) = default;

 private:
  E error_;
};

template <typename E>
Unexpected(E) -> Unexpected<E>;

// Either a T or an E, never both and never neither.
//
// There is no default constructor. Every Expected has to say which side it
// holds, because a silently value-initialized T is exactly the kind of
// plausible-looking wrong answer this type exists to prevent.
template <typename T, typename E>
class Expected {
 public:
  using value_type = T;
  using error_type = E;

  static_assert(!std::is_reference_v<T> && !std::is_reference_v<E>,
                "Expected<T, E> stores both sides by value");
  static_assert(!std::is_same_v<std::remove_cv_t<T>, Unexpected<E>>,
                "Expected<Unexpected<E>, E> cannot disambiguate its own constructors");

  // Implicit on the value side, so a function returning Expected can just
  // `return value;`. The error side stays explicit through Unexpected.
  constexpr Expected(const T& value) : storage_(std::in_place_index<0>, value) {}
  constexpr Expected(T&& value) : storage_(std::in_place_index<0>, std::move(value)) {}

  constexpr Expected(const Unexpected<E>& error)
      : storage_(std::in_place_index<1>, error.error()) {}
  constexpr Expected(Unexpected<E>&& error)
      : storage_(std::in_place_index<1>, std::move(error).error()) {}

  // For a T that cannot be moved into place cheaply, or at all.
  template <typename... Args>
    requires std::is_constructible_v<T, Args...>
  constexpr explicit Expected(std::in_place_t /*tag*/, Args&&... args)
      : storage_(std::in_place_index<0>, std::forward<Args>(args)...) {}

  [[nodiscard]] constexpr bool has_value() const noexcept { return storage_.index() == 0; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

  [[nodiscard]] constexpr T& value() & {
    T* held = std::get_if<0>(&storage_);
    if (held == nullptr) {
      detail::expected_contract_violation(detail::kExpectedNoValue);
    }
    return *held;
  }

  [[nodiscard]] constexpr const T& value() const& {
    const T* held = std::get_if<0>(&storage_);
    if (held == nullptr) {
      detail::expected_contract_violation(detail::kExpectedNoValue);
    }
    return *held;
  }

  [[nodiscard]] constexpr T&& value() && { return std::move(value()); }

  [[nodiscard]] constexpr E& error() & {
    E* held = std::get_if<1>(&storage_);
    if (held == nullptr) {
      detail::expected_contract_violation(detail::kExpectedNoError);
    }
    return *held;
  }

  [[nodiscard]] constexpr const E& error() const& {
    const E* held = std::get_if<1>(&storage_);
    if (held == nullptr) {
      detail::expected_contract_violation(detail::kExpectedNoError);
    }
    return *held;
  }

  [[nodiscard]] constexpr E&& error() && { return std::move(error()); }

  // The accessor with no precondition. Callers that have a sensible fallback
  // use this instead of testing has_value() and then calling value().
  template <typename U>
  [[nodiscard]] constexpr T value_or(U&& fallback) const& {
    const T* held = std::get_if<0>(&storage_);
    return held != nullptr ? *held : static_cast<T>(std::forward<U>(fallback));
  }

  template <typename U>
  [[nodiscard]] constexpr T value_or(U&& fallback) && {
    T* held = std::get_if<0>(&storage_);
    return held != nullptr ? std::move(*held) : static_cast<T>(std::forward<U>(fallback));
  }

  friend constexpr bool operator==(const Expected&, const Expected&) = default;

 private:
  std::variant<T, E> storage_;
};

// "Succeeded, or here is why not."
//
// A great many operations have no result to hand back and only need to say
// whether they worked. Without this specialization every such call site would
// have to invent a placeholder value, which is how a codebase ends up with
// Expected<bool, E> and two different ways to spell failure.
template <typename E>
class Expected<void, E> {
 public:
  using value_type = void;
  using error_type = E;

  static_assert(!std::is_reference_v<E>, "Expected<void, E> stores E by value");

  // Unlike the primary template, a default-constructed Expected<void, E> is
  // meaningful and unambiguous: it is success. There is no value to fabricate.
  constexpr Expected() noexcept : storage_(std::in_place_index<0>) {}

  constexpr Expected(const Unexpected<E>& error)
      : storage_(std::in_place_index<1>, error.error()) {}
  constexpr Expected(Unexpected<E>&& error)
      : storage_(std::in_place_index<1>, std::move(error).error()) {}

  [[nodiscard]] constexpr bool has_value() const noexcept { return storage_.index() == 0; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

  // Asserts success. Same precondition and same contract as the primary
  // template's value(); it just has nothing to return.
  constexpr void value() const {
    if (!has_value()) {
      detail::expected_contract_violation(detail::kExpectedNoValue);
    }
  }

  [[nodiscard]] constexpr E& error() & {
    E* held = std::get_if<1>(&storage_);
    if (held == nullptr) {
      detail::expected_contract_violation(detail::kExpectedNoError);
    }
    return *held;
  }

  [[nodiscard]] constexpr const E& error() const& {
    const E* held = std::get_if<1>(&storage_);
    if (held == nullptr) {
      detail::expected_contract_violation(detail::kExpectedNoError);
    }
    return *held;
  }

  [[nodiscard]] constexpr E&& error() && { return std::move(error()); }

  friend constexpr bool operator==(const Expected&, const Expected&) = default;

 private:
  std::variant<std::monostate, E> storage_;
};

}  // namespace dg
