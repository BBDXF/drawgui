# Golden-image tests. design.md section 7 requires these to be in place from
# P0 - a self-drawn GUI has no DOM to assert against, so pixel comparison is
# the only defence against a layout change quietly breaking a corner.

add_executable(drawgui_golden_test
  golden/golden_image.cpp
  golden/golden_test.cpp)

target_include_directories(drawgui_golden_test PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/golden)

# Skia is linked directly here, not through drawgui, because the harness
# decodes PNG baselines. drawgui itself keeps Skia PRIVATE.
target_link_libraries(drawgui_golden_test PRIVATE
  drawgui::drawgui
  drawgui::skia
  drawgui_warnings)

set(DRAWGUI_GOLDEN_BASELINE_DIR "${CMAKE_CURRENT_LIST_DIR}/golden/baseline")
set(DRAWGUI_GOLDEN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/golden-output")
file(MAKE_DIRECTORY "${DRAWGUI_GOLDEN_OUTPUT_DIR}")

# Runs first by fixture ordering below: if the comparator itself is broken,
# every other golden result is meaningless.
add_test(NAME golden.self_test
  COMMAND drawgui_golden_test --self-test)

# Tolerance is 0. design.md section 5.3.2 chose CPU raster precisely because
# its output is bit-exact and reproducible, so any drift is a real change.
add_test(NAME golden.scenes
  COMMAND drawgui_golden_test
    --baseline-dir "${DRAWGUI_GOLDEN_BASELINE_DIR}"
    --output-dir "${DRAWGUI_GOLDEN_OUTPUT_DIR}"
    --tolerance 0
    --all)

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
