# The test suite: doctest unit tests plus the golden-image runner.
#
# design.md section 7 requires both to be in place from the start - a
# self-drawn GUI has no DOM to assert against, so pixel comparison is the only
# defence against a layout change quietly breaking a corner, and the unit
# tests are what keep the pieces underneath it honest.

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/FetchDoctest.cmake)

# --- Unit tests -------------------------------------------------------------

# golden/golden_image.cpp is compiled in rather than linked from
# drawgui_golden_test: the comparator is what test_golden_image.cpp exercises,
# and there is no library target holding it.
add_executable(drawgui_unit_test
  unit/unit_test_main.cpp
  unit/test_expected.cpp
  unit/test_golden_image.cpp
  unit/test_platform.cpp
  golden/golden_image.cpp)

target_include_directories(drawgui_unit_test PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/golden)

# Skia is linked directly, mirroring drawgui_golden_test, because the golden
# comparator decodes and encodes PNG. drawgui itself keeps Skia PRIVATE.
target_link_libraries(drawgui_unit_test PRIVATE
  drawgui::drawgui
  drawgui::skia
  drawgui::doctest
  drawgui_warnings
  drawgui_sanitizers)

# One CTest entry running every case, deliberately: a per-suite entry would be
# filtered by name, and doctest exits 0 when a filter matches nothing. A typo
# in a filter would then read as a pass, which is the trap design.md section 7
# warns about.
add_test(NAME unit
  COMMAND drawgui_unit_test)

# --- Standalone-header check ------------------------------------------------

# Proves every public platform header is self-contained.
#
# design.md constraint C1 makes include/drawgui/platform/ the seam that keeps
# platform detail out of the core, and a seam is only worth having if a
# consumer can include one piece of it without discovering an undeclared
# ordering requirement. Inside a real translation unit a missing include is
# invisible, because something else already pulled the declaration in; the
# only way to see it is to compile the header alone.
#
# The list is globbed rather than written out, so a header added later is
# covered without anyone remembering to add it - the failure mode a hand-kept
# list has is precisely the one this check exists to catch. Scoped to
# platform/ for now; widening it is a separate change, because the other
# directories have not been verified.
set(_dg_header_check_dir "${CMAKE_CURRENT_BINARY_DIR}/header-check")

file(GLOB _dg_platform_headers CONFIGURE_DEPENDS
  RELATIVE "${CMAKE_CURRENT_LIST_DIR}/../include"
  "${CMAKE_CURRENT_LIST_DIR}/../include/drawgui/platform/*.h")

if(NOT _dg_platform_headers)
  message(FATAL_ERROR
    "No headers found under include/drawgui/platform - the standalone-header "
    "check would pass by having nothing to check.")
endif()

set(_dg_header_check_sources "")
foreach(_dg_header IN LISTS _dg_platform_headers)
  # configure_file rewrites only when the content changes, so re-running
  # CMake does not invalidate the objects.
  set(DRAWGUI_HEADER_CHECK_INCLUDE "${_dg_header}")
  string(REGEX REPLACE "[/.]" "_" _dg_stem "${_dg_header}")
  set(_dg_tu "${_dg_header_check_dir}/${_dg_stem}.cpp")
  configure_file(unit/header_check.cpp.in "${_dg_tu}" @ONLY)
  list(APPEND _dg_header_check_sources "${_dg_tu}")
endforeach()

# OBJECT rather than STATIC: these translation units define no symbols, and
# compiling them is the entire test.
add_library(drawgui_header_check OBJECT ${_dg_header_check_sources})

target_link_libraries(drawgui_header_check PRIVATE
  drawgui::drawgui
  drawgui_warnings)

# --- Golden-image tests -----------------------------------------------------

add_executable(drawgui_golden_test
  golden/golden_image.cpp
  golden/golden_test.cpp)

target_include_directories(drawgui_golden_test PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/golden)

target_link_libraries(drawgui_golden_test PRIVATE
  drawgui::drawgui
  drawgui::skia
  drawgui_warnings
  drawgui_sanitizers)

set(DRAWGUI_GOLDEN_BASELINE_DIR "${CMAKE_CURRENT_LIST_DIR}/golden/baseline")
set(DRAWGUI_GOLDEN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/golden-output")
file(MAKE_DIRECTORY "${DRAWGUI_GOLDEN_OUTPUT_DIR}")

# Tolerance is 0. design.md section 5.3.2 chose CPU raster precisely because
# its output is bit-exact and reproducible, so any drift is a real change.
add_test(NAME golden.scenes
  COMMAND drawgui_golden_test
    --baseline-dir "${DRAWGUI_GOLDEN_BASELINE_DIR}"
    --output-dir "${DRAWGUI_GOLDEN_OUTPUT_DIR}"
    --tolerance 0
    --all)

# The comparator's own tests live in `unit`. If it is broken, every golden
# result is meaningless, so run it first and read its verdict first.
set_tests_properties(golden.scenes PROPERTIES DEPENDS unit)

# --- Sanitizer runtime options ----------------------------------------------

# Only the two entries that actually run an instrumented binary; props.no_drift
# and props.abi_lock shell out to the build system.
#
# UBSan prints a bare `file:line: runtime error: ...` and nothing else by
# default, which names the expression but not the caller that produced the bad
# value. print_stacktrace=1 is what turns that one line into a usable report,
# and it is a runtime option rather than a compile flag, so it has to be set
# here to apply automatically under `ctest`. Measured identical on GCC 15.2 and
# clang 21.1.
#
# Deliberately NOT set: ASAN_OPTIONS=detect_container_overflow=0. That is the
# standard remedy for a std::vector crossing into non-instrumented code - Skia
# here - but it was measured unnecessary rather than assumed so: libstdc++ 15
# gates its container annotations behind _GLIBCXX_SANITIZE_STD_ALLOCATOR, which
# this build does not define, and a deliberate read into a vector's
# [size, capacity) slack went unreported under both compilers. Turning a live
# check off on speculation is how a build stops finding things. See
# doc/development.md for the recipe if a future toolchain does annotate.
#
# Also deliberately NOT set: detect_leaks=0. LeakSanitizer is on by default
# inside ASan on Linux, it is armed here (verified against a deliberate leak),
# and it currently reports nothing - Skia's lazily registered PNG decoder and
# its other process-lifetime state stay reachable at exit, so LSan is content.
# Leak detection is left fully on until something real forces the question.
if(DG_SANITIZE)
  set_tests_properties(unit golden.scenes PROPERTIES
    ENVIRONMENT "UBSAN_OPTIONS=print_stacktrace=1")
endif()

# Fails the build rather than silently regenerating when the committed
# property files have drifted from props/drawgui.props.toml.
add_test(NAME props.no_drift
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target drawgui_props_check)

# props.no_drift only proves the generated files match the TOML; it passes
# just as happily after an id is renumbered. This one guards the numbers
# themselves against props/prop_ids.lock.
add_test(NAME props.abi_lock
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target drawgui_props_lock_check)

# Convenience target for deliberately refreshing baselines. Never invoked by
# CTest: a suite that can rewrite its own expectations proves nothing.
add_custom_target(golden_update
  COMMAND drawgui_golden_test
    --baseline-dir "${DRAWGUI_GOLDEN_BASELINE_DIR}"
    --output-dir "${DRAWGUI_GOLDEN_OUTPUT_DIR}"
    --update
  DEPENDS drawgui_golden_test
  COMMENT "Regenerating golden baselines - review the diff before committing")
