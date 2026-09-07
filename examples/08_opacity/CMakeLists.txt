if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_opacity
    main.cpp
    opacity_check.cpp
    opacity_scene.cpp
    opacity_window.cpp)

  target_link_libraries(drawgui_opacity PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_opacity PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_opacity PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-opacity needs no display, so it runs wherever this target is
  # built. It is NOT the primary gate: this target does not exist without
  # SDL3 and CI has none today, so tests/unit/test_opacity.cpp and
  # tests/unit/test_opacity_damage.cpp carry the same claims into an
  # always-built binary. This entry checks them against the scene a human
  # actually watches fade.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME opacity.verify_demo_scene
      COMMAND drawgui_opacity --verify-opacity)
  endif()
endif()
