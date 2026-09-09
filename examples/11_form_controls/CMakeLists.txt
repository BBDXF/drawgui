if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_form_controls
    main.cpp
    form_check.cpp
    form_scene.cpp
    form_window.cpp)

  target_link_libraries(drawgui_form_controls PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_form_controls PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_form_controls PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-form-controls needs no display, so it runs wherever this target
  # is built - matching examples/10_scrolling's identical rationale for its
  # own verify entry.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME form_controls.verify_demo_scene
      COMMAND drawgui_form_controls --verify-form-controls)
  endif()
endif()
