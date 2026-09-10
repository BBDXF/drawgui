if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_text_input
    main.cpp
    text_field_check.cpp
    text_field_scene.cpp
    text_field_window.cpp)

  target_link_libraries(drawgui_text_input PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_text_input PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_text_input PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-text-input needs no display, so it runs wherever this target is
  # built - matching examples/11_form_controls's identical rationale for its
  # own verify entry.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME text_input.verify_demo_scene
      COMMAND drawgui_text_input --verify-text-input)
  endif()
endif()
