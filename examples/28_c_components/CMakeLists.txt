if(DRAWGUI_HAVE_SDL3)
  # ABI 0.2 phase B from pure C11: menus, dropdowns, tooltips, dialogs,
  # progress bars, tabs, trees and tables through the poll-based event loop.
  add_executable(drawgui_c_components main.c)

  # The deterministic test font, generated into this target's own binary dir
  # exactly as examples/19_c_client does (and for the same reason: the tests'
  # font directory variable is not visible from here).
  find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
  set(DRAWGUI_C_COMPONENTS_FONT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fonts")
  set(DRAWGUI_C_COMPONENTS_FONT_STAMP "${CMAKE_CURRENT_BINARY_DIR}/test-fonts.stamp")
  add_custom_command(
    OUTPUT "${DRAWGUI_C_COMPONENTS_FONT_STAMP}"
    COMMAND ${Python3_EXECUTABLE}
      "${CMAKE_CURRENT_LIST_DIR}/../../tests/fonts/gen_test_fonts.py"
      --out "${DRAWGUI_C_COMPONENTS_FONT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${DRAWGUI_C_COMPONENTS_FONT_STAMP}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../../tests/fonts/gen_test_fonts.py"
    COMMENT "Generating examples/28_c_components' deterministic test fonts")
  add_custom_target(drawgui_c_components_test_fonts
    DEPENDS "${DRAWGUI_C_COMPONENTS_FONT_STAMP}")
  add_dependencies(drawgui_c_components drawgui_c_components_test_fonts)
  target_compile_definitions(drawgui_c_components PRIVATE
    DRAWGUI_C_COMPONENTS_FONT_DIR="${DRAWGUI_C_COMPONENTS_FONT_DIR}")

  set_source_files_properties(main.c PROPERTIES LANGUAGE C)
  set_target_properties(drawgui_c_components PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    LINKER_LANGUAGE CXX
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")
  # drawgui_c_warnings is examples/19_c_client's C warning set.
  target_link_libraries(drawgui_c_components PRIVATE
    drawgui::drawgui
    drawgui_c_warnings
    drawgui_sanitizers)

  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME c_components.verify COMMAND drawgui_c_components --verify)
    set_tests_properties(c_components.verify PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  endif()
endif()
