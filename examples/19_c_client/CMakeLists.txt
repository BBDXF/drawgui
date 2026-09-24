if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_c_client main.c)

  # P6 sub-step a1's own deterministic font, generated the SAME way tests/
  # CMakeLists.txt's drawgui_test_fonts target does (tests/fonts/
  # gen_test_fonts.py writes byte-identical output on every run) - a
  # SEPARATE generation into this target's own binary dir rather than
  # reusing tests/CMakeLists.txt's DRAWGUI_TEST_FONT_DIR, because that
  # variable is set with a plain set() inside tests/CMakeLists.txt's own
  # directory scope and is not visible here (CMake does not propagate a
  # child directory's variables to a sibling one without an explicit
  # PARENT_SCOPE this project's tests/CMakeLists.txt does not ask for).
  find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
  set(DRAWGUI_C_CLIENT_FONT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fonts")
  set(DRAWGUI_C_CLIENT_FONT_STAMP "${CMAKE_CURRENT_BINARY_DIR}/test-fonts.stamp")
  add_custom_command(
    OUTPUT "${DRAWGUI_C_CLIENT_FONT_STAMP}"
    COMMAND ${Python3_EXECUTABLE}
      "${CMAKE_CURRENT_LIST_DIR}/../../tests/fonts/gen_test_fonts.py"
      --out "${DRAWGUI_C_CLIENT_FONT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${DRAWGUI_C_CLIENT_FONT_STAMP}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../../tests/fonts/gen_test_fonts.py"
    COMMENT "Generating examples/19_c_client's own deterministic test fonts")
  add_custom_target(drawgui_c_client_test_fonts DEPENDS "${DRAWGUI_C_CLIENT_FONT_STAMP}")
  add_dependencies(drawgui_c_client drawgui_c_client_test_fonts)
  target_compile_definitions(drawgui_c_client PRIVATE
    DRAWGUI_C_CLIENT_FONT_DIR="${DRAWGUI_C_CLIENT_FONT_DIR}")

  # C, not "C++ compiling a .c file" - CMake's default LANGUAGE for a .c
  # source is already C, but this is stated explicitly because it is the
  # one property this whole example exists to prove: if drawgui.h ever
  # leaks a C++ or Skia type, this line is what fails, loudly, at configure/
  # build time rather than needing a human to notice a stray `virtual` in a
  # header meant to be pure C.
  set_source_files_properties(main.c PROPERTIES LANGUAGE C)
  set_target_properties(drawgui_c_client PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    LINKER_LANGUAGE CXX  # the archive it links (libdrawgui.a) is C++-compiled
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # A dedicated warning set for C: drawgui_warnings carries C++-only flags
  # (-Wold-style-cast, -Wnon-virtual-dtor, -Woverloaded-virtual) that a C
  # front end does not understand, and reusing it here would either warn
  # about every one of them or (worse) silently accept a laxer C build than
  # this slice's own gate asks for. This is deliberately its OWN interface
  # target rather than an addition to drawgui_warnings, for the identical
  # reason drawgui_sanitizers is separate from drawgui_warnings one level up.
  add_library(drawgui_c_warnings INTERFACE)
  target_compile_options(drawgui_c_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  if(DRAWGUI_WERROR)
    target_compile_options(drawgui_c_warnings INTERFACE -Werror)
  endif()

  target_link_libraries(drawgui_c_client PRIVATE
    drawgui::drawgui
    drawgui_c_warnings
    drawgui_sanitizers)

  # examples/19_c_client's own CTest oracle: SDL_VIDEODRIVER=dummy is set
  # here, as a CTest ENVIRONMENT property, rather than by main.c calling
  # setenv() the way examples/14_popup's own C++ oracle does - this file is
  # held to a stricter bar (compiles as plain C11, not C++), and setenv()
  # would need `_POSIX_C_SOURCE`, a reserved identifier clang-tidy's
  # bugprone-reserved-identifier objects to a C file defining, with no
  # alternative spelling POSIX permits. The same headless-real-window
  # technique still applies; only where the
  # environment variable is set differs.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME c_client.verify_demo_scene
      COMMAND drawgui_c_client --verify-c-client)
    set_tests_properties(c_client.verify_demo_scene PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  endif()
endif()
