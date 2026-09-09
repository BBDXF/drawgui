if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_sizing
    main.cpp
    sizing_check.cpp
    sizing_scene.cpp
    sizing_window.cpp)

  target_link_libraries(drawgui_sizing PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_sizing PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_sizing PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-sizing needs no display, so it runs wherever this target is built.
  # It is NOT the primary gate: this target does not exist without SDL3 and CI
  # has none today, so tests/unit/test_sizing.cpp carries the same claims into
  # an always-built binary. This entry checks them against the scene a human
  # actually drags.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME sizing.verify_demo_scene
      COMMAND drawgui_sizing --verify-sizing)
  endif()
endif()
