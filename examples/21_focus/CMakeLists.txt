if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_focus
    main.cpp
    focus_check.cpp
    focus_scene.cpp
    focus_window.cpp
    popup_menu.cpp)

  target_link_libraries(drawgui_focus PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_focus PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_focus PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-focus needs no display: it sets SDL_VIDEODRIVER=dummy itself,
  # matching examples/14_popup's identical rationale (popup_check.cpp's own
  # top comment) - real WindowManager/PopupHost, real event queue, no window
  # server required.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME focus.verify_demo_scene
      COMMAND drawgui_focus --verify-focus)
  endif()
endif()
