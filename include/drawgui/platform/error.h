// PlatformError - why a platform operation could not be carried out.
//
// design.md section 5.14.2 is emphatic that capability must be *queryable
// rather than discovered by trial*: asking for something a backend does not
// have should produce a clear answer at query time, not an ambiguous failure
// code afterwards. This enum is therefore deliberately small, and it is not
// where optional-service support is reported - a T2 service that a backend
// lacks is absent from query_service<T>() (section 5.14.2), and a T1
// operation a backend cannot perform is already visible in PlatformCaps
// before it is called.
//
// What remains here is the set of things that can go wrong at the moment of
// the call, none of which a caller could have predicted from capabilities.

#pragma once

#include <cstdint>

#include "drawgui/base/expected.h"

namespace dg {

enum class PlatformError : std::uint8_t {
  // The arguments describe something the platform cannot make sense of: a
  // non-positive window size, an opacity outside [0, 1], a WindowDesc of kind
  // Dialog with no owner.
  kInvalidArgument,

  // A T1 operation this backend genuinely cannot perform. The corresponding
  // PlatformCaps field said so before the call; this is the answer for a
  // caller that asked anyway.
  kUnsupported,

  // run_loop() on a platform the host owns the loop of, or pump_once() on one
  // drawgui owns it of. See design.md section 5.14.5 and LoopMode.
  kWrongLoopMode,

  kWindowCreationFailed,

  // The WindowId names no live window. Windows are destroyed explicitly, and
  // an event queued before a destroy can still name one afterwards.
  kNoSuchWindow,

  // Nothing to draw into this frame - the window is minimized, occluded, or
  // the compositor has not handed back a surface. Not a defect: the caller
  // skips the frame and tries again. Kept distinct from kBackendFailure so
  // that a normal occlusion cannot end up in an error log.
  kFrameNotAvailable,

  // design.md section 5.17.3: a driver update, sleep/resume or a TDR timeout
  // invalidates the graphics context. Desktop applications meet this
  // routinely, so it is a named outcome rather than a generic failure.
  kGraphicsContextLost,

  // The backend reported a failure of its own that none of the above
  // describes.
  kBackendFailure,
};

// The return type of every platform operation that can fail.
//
// design.md section 3.1's dg::Expected exists for exactly this: a raw pointer
// with nullptr standing in for "something went wrong" throws away the reason
// at the one boundary where the reason is all the caller has.
template <typename T>
using PlatformResult = Expected<T, PlatformError>;

}  // namespace dg
