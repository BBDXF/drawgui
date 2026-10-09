// Status / AppError - how dg::App and dg::Window report a refused request.
//
// User errors are values, never exceptions. The enumerators line up with
// the C ABI's DG_ERR_* family one to one (src/abi maps them), so the C
// layer adds no meaning of its own.

#pragma once

#include <cstdint>
#include <string>

namespace dg {

enum class Status : std::uint8_t {
  kOk,
  kInvalidArgument,
  // The node/window named is not (or no longer) part of this window.
  kInvalidNode,
  kUnknownId,
  kTypeMismatch,
  kValueOutOfRange,
  kNotApplicable,
  kUnsupported,
  kAlreadyAttached,
  kNoActiveTheme,
};

struct AppError {
  Status status = Status::kInvalidArgument;
  std::string message;
};

}  // namespace dg
