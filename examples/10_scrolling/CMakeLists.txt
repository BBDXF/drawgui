if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_scrolling
    main.cpp
    scroll_check.cpp
    scroll_scene.cpp
    scroll_window.cpp)

  target_link_libraries(drawgui_scrolling PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_scrolling PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_scrolling PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-scrolling needs no display, so it runs wherever this target is
  # built. It is NOT the primary gate: this target does not exist without
  # SDL3 and CI has none today, so tests/unit/test_scroll.cpp and
  # tests/unit/test_layout.cpp's scroll_axis cases carry the same claims into
  # an always-built binary. This entry checks them against the scene a human
  # actually scrolls.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME scrolling.verify_demo_scene
      COMMAND drawgui_scrolling --verify-scrolling)
  endif()
endif()
