# The test suite: doctest unit tests plus the golden-image runner.
#
# design.md section 7 requires both to be in place from the start - a
# self-drawn GUI has no DOM to assert against, so pixel comparison is the only
# defence against a layout change quietly breaking a corner, and the unit
# tests are what keep the pieces underneath it honest.

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/FetchDoctest.cmake)

# --- Test fonts -------------------------------------------------------------

# The font-fallback tests resolve against FOUR FONTS THIS BUILD GENERATES, not
# against the fonts of whichever machine is running the suite.
#
# The reason is that the resolver's answer is a function of the fonts it was
# given, so an assertion about /usr/share/fonts is green here and red on a CI
# image with a different font package set - and the usual way out, asserting
# only that the call returned something, passes just as happily on a tofu box.
# Generated fonts let a test pin an exact family name and an exact glyph id.
#
# tests/fonts/gen_test_fonts.py writes byte-identical output on every run, so
# this is not a source of build nondeterminism.
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

set(DRAWGUI_TEST_FONT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fonts")
set(DRAWGUI_TEST_FONT_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/fonts/gen_test_fonts.py")
set(DRAWGUI_TEST_FONT_STAMP "${CMAKE_CURRENT_BINARY_DIR}/test-fonts.stamp")

add_custom_command(
  OUTPUT "${DRAWGUI_TEST_FONT_STAMP}"
  COMMAND ${Python3_EXECUTABLE} "${DRAWGUI_TEST_FONT_SCRIPT}" --out "${DRAWGUI_TEST_FONT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E touch "${DRAWGUI_TEST_FONT_STAMP}"
  DEPENDS "${DRAWGUI_TEST_FONT_SCRIPT}"
  COMMENT "Generating the fallback test fonts")

add_custom_target(drawgui_test_fonts DEPENDS "${DRAWGUI_TEST_FONT_STAMP}")

# --- Unit tests -------------------------------------------------------------

# golden/golden_image.cpp is compiled in rather than linked from
# drawgui_golden_test: the comparator is what test_golden_image.cpp exercises,
# and there is no library target holding it.
#
# examples/03_damage_repaint/scene.cpp is compiled in for a different reason.
# The damage verification compares a partially repainted frame against a fully
# repainted one byte for byte, and that comparison is worth much more when it
# runs the scene that is actually on screen than when it runs a second scene
# written to be easy to verify. The file needs neither SDL3 nor a display -
# only the render tree - which is what lets the verification be a CTest entry
# on a headless machine while the demo itself cannot be one.
add_executable(drawgui_unit_test
  unit/unit_test_main.cpp
  unit/test_expected.cpp
  unit/test_utf8.cpp
  unit/test_font_fallback.cpp
  unit/test_font_coverage.cpp
  unit/test_golden_image.cpp
  unit/test_pixel_geometry.cpp
  unit/test_damage.cpp
  unit/test_damage_repaint.cpp
  unit/test_clip_damage.cpp
  unit/test_clip.cpp
  unit/test_opacity.cpp
  unit/test_opacity_damage.cpp
  unit/test_hit_test.cpp
  unit/test_interaction.cpp
  unit/test_text_damage.cpp
  unit/test_widget_pipeline.cpp
  unit/test_layout.cpp
  unit/test_layout_identity.cpp
  unit/test_wrap.cpp
  unit/test_sizing.cpp
  unit/test_scroll.cpp
  unit/test_list.cpp
  unit/test_form_controls.cpp
  unit/test_text_input.cpp
  unit/test_border_paint.cpp
  unit/test_props_parity.cpp
  unit/test_props_boundary.cpp
  unit/test_props_damage.cpp
  unit/test_complex_props.cpp
  unit/test_image.cpp
  unit/test_animation.cpp
  unit/test_theme.cpp
  golden/golden_image.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../examples/03_damage_repaint/scene.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../examples/04_layout/layout_scene.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../examples/05_widgets/widget_scene.cpp)

target_include_directories(drawgui_unit_test PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/golden
  ${CMAKE_CURRENT_LIST_DIR}/../examples/03_damage_repaint
  ${CMAKE_CURRENT_LIST_DIR}/../examples/04_layout
  ${CMAKE_CURRENT_LIST_DIR}/../examples/05_widgets

  # src/ so that the clip geometry can be tested DIRECTLY rather than only
  # through the pixels it produces. Two of the fifteen defect injections for
  # this slice survived because the only observers of fit_radii() were a
  # rasterizer and a hit test that both degrade a bad radius to a square
  # corner in the same way - the arithmetic itself had no test at all.
  # clip_shape.h names no Skia type, so this pulls nothing else in.
  ${CMAKE_CURRENT_LIST_DIR}/../src)

# Skia is linked directly, mirroring drawgui_golden_test, because the golden
# comparator decodes and encodes PNG. drawgui itself keeps Skia PRIVATE.
target_link_libraries(drawgui_unit_test PRIVATE
  drawgui::drawgui
  drawgui::skia
  drawgui::doctest
  drawgui_warnings
  drawgui_sanitizers)

# The generated font directory, and the system one the coverage test scans.
#
# DRAWGUI_SYSTEM_FONT_DIR is a cache variable rather than a literal so that a
# machine keeping its fonts elsewhere can point the test at them instead of
# having the test quietly report "nothing to check" - which is how a coverage
# gate becomes a tautology.
set(DRAWGUI_SYSTEM_FONT_DIR "/usr/share/fonts"
  CACHE PATH "Directory the system-font coverage test scans")

target_compile_definitions(drawgui_unit_test PRIVATE
  DG_TEST_FONT_DIR="${DRAWGUI_TEST_FONT_DIR}"
  DG_SYSTEM_FONT_DIR="${DRAWGUI_SYSTEM_FONT_DIR}")

add_dependencies(drawgui_unit_test drawgui_test_fonts)

# One CTest entry running every case, deliberately: a per-suite entry would be
# filtered by name, and doctest exits 0 when a filter matches nothing. A typo
# in a filter would then read as a pass, which is the trap design.md section 7
# warns about.
add_test(NAME unit
  COMMAND drawgui_unit_test)

# --- Layout identity ---------------------------------------------------------

# The acceptance gate for incremental layout, given a name of its own in the
# CTest list. The same comparison runs inside `unit`; this target exists so
# that `ctest -R layout` answers the question directly instead of requiring a
# reader to know it is one case inside a suite called `unit`.
#
# Built unconditionally. It shares its scene with examples/04_layout but needs
# neither SDL3 nor a display, which is what lets it gate CI while the demo it
# describes cannot.
add_executable(drawgui_layout_identity
  layout/layout_identity_main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../examples/04_layout/layout_check.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../examples/04_layout/layout_scene.cpp)

target_include_directories(drawgui_layout_identity PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../examples/04_layout)

target_link_libraries(drawgui_layout_identity PRIVATE
  drawgui::drawgui
  drawgui_warnings
  drawgui_sanitizers)

add_test(NAME layout.incremental_equals_full
  COMMAND drawgui_layout_identity)

# --- Widget interaction ------------------------------------------------------

# The two acceptance gates for sub-step 3: interaction-driven repaint against a
# full repaint byte for byte, and hit testing against a brute-force paint-order
# oracle at every pixel of the demo scene.
#
# Built unconditionally, for the reason the layout gate is. It shares
# widget_scene.cpp - the scene AND the pointer routing - with the demo a human
# clicks, so the thing verified here is the thing on screen; but it opens no
# window, so CI can run it.
add_executable(drawgui_widget_identity
  widget/widget_identity_main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../examples/05_widgets/widget_check.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../examples/05_widgets/widget_scene.cpp)

target_include_directories(drawgui_widget_identity PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../examples/05_widgets

  # src/ so that the clip geometry can be tested DIRECTLY rather than only
  # through the pixels it produces. Two of the fifteen defect injections for
  # this slice survived because the only observers of fit_radii() were a
  # rasterizer and a hit test that both degrade a bad radius to a square
  # corner in the same way - the arithmetic itself had no test at all.
  # clip_shape.h names no Skia type, so this pulls nothing else in.
  ${CMAKE_CURRENT_LIST_DIR}/../src)

target_link_libraries(drawgui_widget_identity PRIVATE
  drawgui::drawgui
  drawgui_warnings
  drawgui_sanitizers)

add_test(NAME widgets.interaction_equals_full
  COMMAND drawgui_widget_identity)

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
  set_tests_properties(unit golden.scenes layout.incremental_equals_full
    widgets.interaction_equals_full PROPERTIES
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

# props.abi_lock proves the committed table honours the lock. It cannot prove
# the lock would notice if it did not, because it only ever sees a correct
# table. This entry tampers on purpose and requires a rejection every time,
# including the exact case that silently passed before commit c5ce347.
add_test(NAME props.lock_selftest
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target drawgui_props_lock_selftest)

# The theme token family's own three gates, mirroring props.no_drift/
# props.abi_lock/consistency exactly (cmake/GenerateTheme.cmake,
# design.md section 5.7.7).
add_test(NAME theme.no_drift
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target drawgui_theme_check)

add_test(NAME theme.abi_lock
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target drawgui_theme_lock_check)

# design.md section 5.7.7 (C4): the builtin theme must cover every schema
# token - the gate this slice's task names by name (tools/check_consistency.py).
add_test(NAME theme.consistency
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target drawgui_theme_consistency)

# The C ABI family's own two gates, mirroring props.no_drift/props.abi_lock
# exactly (cmake/GenerateAbi.cmake, design.md section 5.8 decision 5).
add_test(NAME abi.no_drift
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target drawgui_abi_check)

# abi.no_drift only proves the generated files match abi/drawgui.def.toml; it
# regenerates just as happily after a function is re-signed or a struct field
# reordered. This one guards the definitions themselves against
# abi/drawgui_abi.lock (design.md section 5.8 decision 4).
add_test(NAME abi.abi_lock
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target drawgui_abi_lock_check)

# Convenience target for deliberately refreshing baselines. Never invoked by
# CTest: a suite that can rewrite its own expectations proves nothing.
add_custom_target(golden_update
  COMMAND drawgui_golden_test
    --baseline-dir "${DRAWGUI_GOLDEN_BASELINE_DIR}"
    --output-dir "${DRAWGUI_GOLDEN_OUTPUT_DIR}"
    --update
  DEPENDS drawgui_golden_test
  COMMENT "Regenerating golden baselines - review the diff before committing")
