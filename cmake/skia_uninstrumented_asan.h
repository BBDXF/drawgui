// Force-included (-include) into first-party code only when DG_SANITIZE is ON.
//
// The prebuilt Skia archive is compiled without ASan, but Skia's
// include/private/SkASAN.h turns on SK_SANITIZE_ADDRESS whenever the *including*
// translation unit is ASan-instrumented, and skia_private::TArray then grows an
// extra `bool fPoisoned` member. That changes sizeof(TArray) and therefore the
// layout of every Skia class that embeds one (SkSVGContainer::fChildren and so
// SkSVGSVG, paragraph internals, ...): inline accessors compiled here read fields
// at offsets the archive never wrote (e.g. SkSVGSVG::getViewBox() read 8 bytes
// past the end of the object).
//
// Claiming SkASAN.h's include guard first keeps SK_SANITIZE_ADDRESS undefined,
// so Skia headers see the same configuration the archive was built with. The
// helpers below are the uninstrumented branch of SkASAN.h, verbatim in effect.
#ifndef SkASAN_DEFINED
#define SkASAN_DEFINED

#include <cstddef>

static inline void sk_asan_poison_memory_region(void const volatile*, size_t) {}
static inline void sk_asan_unpoison_memory_region(void const volatile*, size_t) {}
static inline int sk_asan_address_is_poisoned(void const volatile*) { return 0; }

#endif  // SkASAN_DEFINED
