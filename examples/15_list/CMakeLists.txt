if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_list
    main.cpp
    list_check.cpp
    list_scene.cpp
    list_window.cpp)

  target_link_libraries(drawgui_list PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_list PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_list PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-list needs no display, so it runs wherever this target is
  # built - the same shape every other demo's headless gate has.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME list.verify_demo_scene
      COMMAND drawgui_list --verify-list)
  endif()
endif()
