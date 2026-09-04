// Unit tests for dg::Expected<T, E>.
//
// design.md section 7 warns about `check(true, ...)` style tests. Every case
// below is written so that it goes red if the behaviour it names breaks: the
// destructor cases count real destructions rather than asserting a type
// trait, the move cases observe ownership transfer, and the ambiguity case
// uses Expected<int, int>, where a value and an error are indistinguishable
// unless the error side names itself.
//
// One behaviour is deliberately not covered: value() on an error state, and
// error() on a value state, terminate the process by design (see the contract
// note in expected.h). doctest has no death-test facility, so a test for that
// path would have to fork, and the payoff would be proving that std::abort
// aborts.

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <doctest/doctest.h>

#include "drawgui/base/expected.h"

namespace {

// The shape of error that motivated Expected in the first place:
// RasterSurface::create currently returns std::optional and throws away the
// reason. It is not refactored here, but the error type it will need is what
// these tests exercise.
enum class SurfaceError : std::uint8_t {
  kInvalidSize,
  kAllocationFailed,
};

struct SurfaceFailure {
  SurfaceError code = SurfaceError::kInvalidSize;
  std::string detail;

  friend bool operator==(const SurfaceFailure&, const SurfaceFailure&) = default;
};

// Counts its own destruction into a caller-supplied counter, so a test can
// observe that the active alternative really was destroyed - once, and only
// the active one. The counter is a local of the test case rather than a
// global, so cases stay independent of each other and of their order.
//
// A moved-from Tracked drops the counter, which makes "how many objects
// actually owned the resource" the number the tests assert on.
class Tracked {
 public:
  explicit Tracked(int* destructions) noexcept : destructions_(destructions) {}

  Tracked(const Tracked&) noexcept = default;
  Tracked& operator=(const Tracked&) noexcept = default;

  Tracked(Tracked&& other) noexcept : destructions_(other.destructions_) {
    other.destructions_ = nullptr;
  }

  Tracked& operator=(Tracked&& other) noexcept {
    if (this != &other) {
      destructions_ = other.destructions_;
      other.destructions_ = nullptr;
    }
    return *this;
  }

  ~Tracked() {
    if (destructions_ != nullptr) {
      *destructions_ += 1;
    }
  }

 private:
  int* destructions_ = nullptr;
};

// --- Compile-time properties ------------------------------------------------

// Every Expected must say which side it holds. A default-constructed one
// would hold a value-initialized T, which is precisely the plausible-looking
// wrong answer this type exists to prevent.
static_assert(!std::is_default_constructible_v<dg::Expected<int, SurfaceError>>);

// Except for the void specialization, where the default state is success and
// there is no value to fabricate.
static_assert(std::is_default_constructible_v<dg::Expected<void, SurfaceError>>);

// Two trivial alternatives must produce a trivial Expected: a type that costs
// a heap allocation or a non-trivial copy would not be usable on the hot
// paths this exists for.
static_assert(std::is_trivially_copyable_v<dg::Expected<int, SurfaceError>>);
static_assert(std::is_trivially_destructible_v<dg::Expected<int, SurfaceError>>);

// And a non-trivial alternative must not be silently forgotten.
static_assert(!std::is_trivially_destructible_v<dg::Expected<std::string, SurfaceError>>);

// Move-only values have to work; RasterSurface is move-only.
static_assert(std::is_move_constructible_v<dg::Expected<std::unique_ptr<int>, SurfaceError>>);
static_assert(!std::is_copy_constructible_v<dg::Expected<std::unique_ptr<int>, SurfaceError>>);

// --- Constant evaluation ----------------------------------------------------

constexpr dg::Expected<int, SurfaceError> kConstexprValue = 7;
static_assert(kConstexprValue.has_value());
static_assert(static_cast<bool>(kConstexprValue));
static_assert(kConstexprValue.value() == 7);
static_assert(kConstexprValue.value_or(-1) == 7);

constexpr dg::Expected<int, SurfaceError> kConstexprError{
    dg::Unexpected{SurfaceError::kAllocationFailed}};
static_assert(!kConstexprError.has_value());
static_assert(!static_cast<bool>(kConstexprError));
static_assert(kConstexprError.error() == SurfaceError::kAllocationFailed);
static_assert(kConstexprError.value_or(-1) == -1);

constexpr dg::Expected<void, SurfaceError> kConstexprSuccess;
static_assert(kConstexprSuccess.has_value());

}  // namespace

TEST_CASE("Expected carrying a value") {
  const dg::Expected<int, SurfaceError> result = 42;

  REQUIRE(result.has_value());
  CHECK(static_cast<bool>(result));
  CHECK(result.value() == 42);
  CHECK(result.value_or(-1) == 42);
}

TEST_CASE("Expected carrying an error keeps the reason") {
  const dg::Expected<int, SurfaceFailure> result{
      dg::Unexpected{SurfaceFailure{SurfaceError::kInvalidSize, "width must be positive"}}};

  REQUIRE_FALSE(result.has_value());
  CHECK_FALSE(static_cast<bool>(result));
  CHECK(result.error().code == SurfaceError::kInvalidSize);
  CHECK(result.error().detail == "width must be positive");

  // The whole point over std::optional: the fallback is available and the
  // reason survives alongside it.
  CHECK(result.value_or(-1) == -1);
}

TEST_CASE("Expected<int, int> tells a value from an error") {
  const dg::Expected<int, int> value_five = 5;
  const dg::Expected<int, int> error_five{dg::Unexpected{5}};

  CHECK(value_five.has_value());
  CHECK_FALSE(error_five.has_value());
  CHECK(value_five.value() == 5);
  CHECK(error_five.error() == 5);
  CHECK_FALSE(value_five == error_five);
}

TEST_CASE("Expected compares equal only within the same side") {
  const dg::Expected<int, SurfaceError> one = 1;
  const dg::Expected<int, SurfaceError> also_one = 1;
  const dg::Expected<int, SurfaceError> two = 2;
  const dg::Expected<int, SurfaceError> failed{dg::Unexpected{SurfaceError::kInvalidSize}};
  const dg::Expected<int, SurfaceError> also_failed{dg::Unexpected{SurfaceError::kInvalidSize}};

  CHECK(one == also_one);
  CHECK(one != two);
  CHECK(failed == also_failed);
  CHECK(one != failed);
}

TEST_CASE("Expected builds its value in place") {
  const dg::Expected<std::string, SurfaceError> built{std::in_place, 3, 'x'};

  REQUIRE(built.has_value());
  CHECK(built.value() == "xxx");
}

TEST_CASE("Expected destroys exactly the alternative it holds") {
  int value_destructions = 0;
  int error_destructions = 0;

  {
    const dg::Expected<Tracked, Tracked> holds_value{Tracked{&value_destructions}};
    REQUIRE(holds_value.has_value());
    CHECK(value_destructions == 0);
  }
  CHECK(value_destructions == 1);
  CHECK(error_destructions == 0);

  {
    const dg::Expected<Tracked, Tracked> holds_error{
        dg::Unexpected{Tracked{&error_destructions}}};
    REQUIRE_FALSE(holds_error.has_value());
    CHECK(error_destructions == 0);
  }
  CHECK(value_destructions == 1);
  CHECK(error_destructions == 1);
}

TEST_CASE("Expected<void, E> destroys its error") {
  int destructions = 0;

  {
    const dg::Expected<void, Tracked> failed{dg::Unexpected{Tracked{&destructions}}};
    REQUIRE_FALSE(failed.has_value());
    CHECK(destructions == 0);
  }
  CHECK(destructions == 1);
}

TEST_CASE("Copying an Expected produces an independent value") {
  int destructions = 0;

  {
    const dg::Expected<Tracked, int> original{Tracked{&destructions}};
    {
      dg::Expected<Tracked, int> copy{original};
      REQUIRE(copy.has_value());

      // Overwriting the copy with the other side must destroy the Tracked the
      // copy owned, and must not touch the original's.
      copy = dg::Expected<Tracked, int>{dg::Unexpected{0}};
      CHECK_FALSE(copy.has_value());
      CHECK(destructions == 1);
      CHECK(original.has_value());
    }
    CHECK(destructions == 1);
  }
  CHECK(destructions == 2);
}

TEST_CASE("Moving an Expected transfers ownership rather than duplicating it") {
  int destructions = 0;

  {
    dg::Expected<Tracked, int> source{Tracked{&destructions}};
    {
      dg::Expected<Tracked, int> moved{std::move(source)};
      CHECK(moved.has_value());
    }
    // Exactly one object ever owned the counter, so the moved-from source
    // must not contribute a second destruction.
    CHECK(destructions == 1);
  }
  CHECK(destructions == 1);
}

TEST_CASE("Expected carries a move-only value") {
  dg::Expected<std::unique_ptr<int>, SurfaceError> result{std::make_unique<int>(9)};
  REQUIRE(result.has_value());

  std::unique_ptr<int> taken = std::move(result).value();
  REQUIRE(taken != nullptr);
  CHECK(*taken == 9);
}

TEST_CASE("value_or on an rvalue moves the held value out") {
  dg::Expected<std::string, SurfaceError> held{std::string(64, 'a')};
  const std::string kept = std::move(held).value_or("fallback");
  CHECK(kept == std::string(64, 'a'));

  dg::Expected<std::string, SurfaceError> failed{dg::Unexpected{SurfaceError::kInvalidSize}};
  const std::string fallback = std::move(failed).value_or("fallback");
  CHECK(fallback == "fallback");
}

TEST_CASE("Expected<void, E> distinguishes success from failure") {
  const dg::Expected<void, SurfaceFailure> succeeded;
  REQUIRE(succeeded.has_value());
  CHECK(static_cast<bool>(succeeded));
  succeeded.value();  // asserts success; must not abort here

  const dg::Expected<void, SurfaceFailure> failed{
      dg::Unexpected{SurfaceFailure{SurfaceError::kAllocationFailed, "out of memory"}}};
  REQUIRE_FALSE(failed.has_value());
  CHECK(failed.error().code == SurfaceError::kAllocationFailed);
  CHECK(failed.error().detail == "out of memory");
  CHECK(succeeded != failed);
}

TEST_CASE("Unexpected hands its error over without copying it twice") {
  int destructions = 0;

  {
    dg::Unexpected<Tracked> reason{Tracked{&destructions}};
    {
      const dg::Expected<int, Tracked> failed{std::move(reason)};
      CHECK_FALSE(failed.has_value());
    }
    CHECK(destructions == 1);
  }
  CHECK(destructions == 1);
}
