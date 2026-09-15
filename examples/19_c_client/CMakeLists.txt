if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_c_client main.c)

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
  # technique doc/popup.md measured still applies; only where the
  # environment variable is set differs.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME c_client.verify_demo_scene
      COMMAND drawgui_c_client --verify-c-client)
    set_tests_properties(c_client.verify_demo_scene PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  endif()
endif()
