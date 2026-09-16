if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_theme_package
    main.cpp
    theme_package_check.cpp
    theme_package_scene.cpp
    theme_package_window.cpp)

  target_link_libraries(drawgui_theme_package PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_theme_package PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  # The fixture package (fixtures/mytheme/) is read-only source, checked
  # into the repo; --verify-theme-package copies it to a writable temp
  # directory before editing anything, so a compiled-in absolute path to
  # the SOURCE tree is what every mode (interactive/--dump-png/--verify)
  # defaults to when --package-dir is not given - the same DG_TEST_FONT_DIR
  # shape tests/CMakeLists.txt already uses for a source-tree path a test
  # binary needs to find regardless of its own build/install location.
  target_compile_definitions(drawgui_theme_package PRIVATE
    DRAWGUI_THEME_PACKAGE_FIXTURE_DIR="${CMAKE_CURRENT_LIST_DIR}/fixtures/mytheme")

  set_target_properties(drawgui_theme_package PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME theme_package.verify_demo_scene
      COMMAND drawgui_theme_package --verify-theme-package)
  endif()
endif()
