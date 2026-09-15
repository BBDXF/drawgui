if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_complex_properties
    main.cpp
    cprops_check.cpp
    cprops_scene.cpp
    cprops_window.cpp)

  target_link_libraries(drawgui_complex_properties PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_complex_properties PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_complex_properties PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-complex-properties needs no display, so it runs wherever this
  # target is built - matching image.verify_demo_scene's own reasoning: this
  # target does not exist without SDL3 and CI has none today, so
  # tests/unit/test_complex_props.cpp carries the same claims about the
  # channel into an always-built binary. This entry checks them against the
  # scene a human actually looks at.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME complex_properties.verify_demo_scene
      COMMAND drawgui_complex_properties --verify-complex-properties)
  endif()
endif()
