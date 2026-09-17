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
  # ordinary window and drives the overlay branch for real; the one native
  # popup/dialog surface this file probes (PopupHost::show() with
  # caps.native_popup forced false) is deliberately not attempted under
  # dummy at all - examples/14_popup/21_focus/22_dropdown_menu/
  # 23_menu_tooltip_dialog already establish that SDL_CreatePopupWindow/
  # SDL_SetWindowParent fail loudly under it, so this oracle does not
  # re-attempt a call already known to fail here.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME showcase.verify_demo_scene
      COMMAND drawgui_showcase --verify-showcase)
  endif()
endif()
