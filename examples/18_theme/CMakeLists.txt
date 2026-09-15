if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_theme
    main.cpp
    theme_check.cpp
    theme_scene.cpp
    theme_window.cpp)

  target_link_libraries(drawgui_theme PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_theme PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_theme PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-theme needs no display, matching every prior slice's own
  # verify_demo_scene entry (image.verify_demo_scene, complex_properties.
  # verify_demo_scene, ...): this target does not exist without SDL3, but
  # the check itself does not touch the window manager.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME theme.verify_demo_scene
      COMMAND drawgui_theme --verify-theme)
  endif()
endif()
