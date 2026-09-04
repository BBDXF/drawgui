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
  golden/golden_image.cpp)

target_include_directories(drawgui_unit_test PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/golden)

# Skia is linked directly, mirroring drawgui_golden_test, because the golden
# comparator decodes and encodes PNG. drawgui itself keeps Skia PRIVATE.
target_link_libraries(drawgui_unit_test PRIVATE
  drawgui::drawgui
  drawgui::skia
  drawgui::doctest
  drawgui_warnings)

# One CTest entry running every case, deliberately: a per-suite entry would be
# filtered by name, and doctest exits 0 when a filter matches nothing. A typo
# in a filter would then read as a pass, which is the trap design.md section 7
# warns about.
add_test(NAME unit
  COMMAND drawgui_unit_test)

# --- Golden-image tests -----------------------------------------------------

add_executable(drawgui_golden_test
  golden/golden_image.cpp
  golden/golden_test.cpp)

target_include_directories(drawgui_golden_test PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/golden)

target_link_libraries(drawgui_golden_test PRIVATE
  drawgui::drawgui
  drawgui::skia
  drawgui_warnings)

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
