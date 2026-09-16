if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_multiline_text
    main.cpp
    multiline_check.cpp
    multiline_scene.cpp
    multiline_window.cpp)

  target_link_libraries(drawgui_multiline_text PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_multiline_text PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_multiline_text PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-multiline-text needs no display and scans the SAME real system
  # font directory examples/06_font_fallback already does - a machine with no
  # fonts under /usr/share/fonts is not one this project's own font-fallback
  # demo runs on either, so this check accepts the same precondition rather
  # than inventing a second one.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME multiline_text.verify_demo_scene
      COMMAND drawgui_multiline_text --verify-multiline-text)
  endif()
endif()
