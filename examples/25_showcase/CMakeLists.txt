if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_showcase
    main.cpp
    assets.cpp
    showcase.cpp
    showcase_check.cpp)

  # skia2 for the encoders that produce the sample PNG/JPEG/WebP at start-up.
  target_link_libraries(drawgui_showcase PRIVATE
    drawgui::drawgui
    skia2::skia2
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_showcase PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../common")

  set_target_properties(drawgui_showcase PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-showcase runs on SDL's dummy video driver and drives every tab
  # with injected input: no display needed.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME showcase.verify_demo_scene
      COMMAND drawgui_showcase --verify-showcase)
  endif()
endif()
