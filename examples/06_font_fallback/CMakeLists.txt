if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_font_fallback
    main.cpp
    font_check.cpp
    font_scene.cpp)

  # No drawgui::skia here. Everything on screen is a render-tree node, and the
  # PNG dump goes through RasterSurface::encode_png(), so this example needs no
  # Skia type of its own - which is the state examples 03 to 05 were reaching
  # towards and the first one to actually get there.
  target_link_libraries(drawgui_font_fallback PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_font_fallback PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_font_fallback PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-fallback needs no display, so a machine with SDL3 runs the check
  # against the binary a human actually looks at.
  #
  # The PRIMARY gate is NOT this entry - this target does not exist without
  # SDL3, and CI has none today. tests/unit/test_font_coverage.cpp and
  # tests/unit/test_font_fallback.cpp are compiled into the always-built unit
  # binary, so the coverage check cannot silently stop running.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME fonts.verify_demo_scene
      COMMAND drawgui_font_fallback --verify-fallback)
  endif()
endif()
