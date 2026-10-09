if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_data_widgets
    main.cpp)

  target_link_libraries(drawgui_data_widgets PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_data_widgets PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../common")

  set_target_properties(drawgui_data_widgets PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify runs the runtime on SDL's dummy video driver and drives the
  # controls with injected input: no display.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME data_widgets.verify_demo_scene
      COMMAND drawgui_data_widgets --verify)
  endif()
endif()
