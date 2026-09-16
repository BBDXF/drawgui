if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_dropdown_menu
    main.cpp
    dropdown_check.cpp
    dropdown_options.cpp
    dropdown_scene.cpp
    dropdown_window.cpp)

  target_link_libraries(drawgui_dropdown_menu PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_dropdown_menu PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_dropdown_menu PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-dropdown needs no display: it sets SDL_VIDEODRIVER=dummy
  # itself, matching examples/14_popup/examples/21_focus's identical
  # rationale - real WindowManager/PopupHost, real dg::Focus traversal, no
  # window server required.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME dropdown_menu.verify_demo_scene
      COMMAND drawgui_dropdown_menu --verify-dropdown)
  endif()
endif()
