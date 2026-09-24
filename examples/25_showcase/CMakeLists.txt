if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_showcase
    context_menu.cpp
    main.cpp
    profile_dialog.cpp
    showcase_check.cpp
    showcase_scene.cpp
    showcase_window.cpp
    sort_options.cpp
    tooltip_content.cpp)

  target_link_libraries(drawgui_showcase PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_showcase PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_showcase PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-showcase needs no display: SDL_VIDEODRIVER=dummy opens an
  # ordinary window and drives the popup/tooltip/dialog paths - overlay-only
  # (native OS popup windows were removed), so every path
  # this oracle probes is exercised for real under the dummy driver.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME showcase.verify_demo_scene
      COMMAND drawgui_showcase --verify-showcase)
  endif()
endif()
