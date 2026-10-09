if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_form_controls main.cpp)

  target_link_libraries(drawgui_form_controls PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_form_controls PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../common")

  set_target_properties(drawgui_form_controls PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-form-controls needs no display: it sets SDL_VIDEODRIVER=dummy
  # itself and drives the real dg::App pump.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME form_controls.verify_demo_scene
      COMMAND drawgui_form_controls --verify-form-controls)
  endif()
endif()
