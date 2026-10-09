if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_text_area main.cpp)

  # The deterministic test fonts --verify-text-area types with, generated
  # the same way examples/19_c_client does (a sibling directory cannot see
  # tests/CMakeLists.txt's own font directory variable).
  find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
  set(DRAWGUI_TEXT_AREA_FONT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fonts")
  set(DRAWGUI_TEXT_AREA_FONT_STAMP "${CMAKE_CURRENT_BINARY_DIR}/test-fonts.stamp")
  add_custom_command(
    OUTPUT "${DRAWGUI_TEXT_AREA_FONT_STAMP}"
    COMMAND ${Python3_EXECUTABLE}
      "${CMAKE_CURRENT_LIST_DIR}/../../tests/fonts/gen_test_fonts.py"
      --out "${DRAWGUI_TEXT_AREA_FONT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${DRAWGUI_TEXT_AREA_FONT_STAMP}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../../tests/fonts/gen_test_fonts.py"
    COMMENT "Generating examples/26_text_area's deterministic test fonts")
  add_custom_target(drawgui_text_area_test_fonts DEPENDS "${DRAWGUI_TEXT_AREA_FONT_STAMP}")
  add_dependencies(drawgui_text_area drawgui_text_area_test_fonts)
  target_compile_definitions(drawgui_text_area PRIVATE
    DRAWGUI_TEXT_AREA_FONT_DIR="${DRAWGUI_TEXT_AREA_FONT_DIR}")

  target_link_libraries(drawgui_text_area PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  set_target_properties(drawgui_text_area PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME text_area.verify_demo_scene
      COMMAND drawgui_text_area --verify-text-area)
    set_tests_properties(text_area.verify_demo_scene PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  endif()
endif()
