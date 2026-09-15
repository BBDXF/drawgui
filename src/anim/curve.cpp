#include "drawgui/anim/curve.h"

namespace dg {

float evaluate_curve(dg_curve_id curve_id, float t) {
  switch (curve_id) {
    case kCurveLinear:
      return t;
    case kCurveEaseIn:
      return t * t;
    case kCurveEaseOut:
      return 1.0F - ((1.0F - t) * (1.0F - t));
    case kCurveEaseInOut:
      return t < 0.5F ? 2.0F * t * t
                      : 1.0F - (((-2.0F * t) + 2.0F) * ((-2.0F * t) + 2.0F) / 2.0F);
    case kCurveInvalid:
    default:
      return t;
  }
}

}  // namespace dg
