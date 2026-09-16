if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_menu_tooltip_dialog
    main.cpp
    dialog_panel.cpp
    menu_check.cpp
    menu_rows.cpp
    menu_scene.cpp
    menu_window.cpp
    tooltip_content.cpp)

  target_link_libraries(drawgui_menu_tooltip_dialog PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_menu_tooltip_dialog PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_menu_tooltip_dialog PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-menus needs no display: it sets SDL_VIDEODRIVER=dummy itself,
  # matching examples/22_dropdown_menu/examples/21_focus's identical
  # rationale - real WindowManager/PopupHost/dg::Focus, no window server
  # required.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME menu_tooltip_dialog.verify_demo_scene
      COMMAND drawgui_menu_tooltip_dialog --verify-menus)
  endif()
endif()
