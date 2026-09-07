if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_multi_window main.cpp)

  target_link_libraries(drawgui_multi_window PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  # Keep binaries in build/examples/ for backwards compatibility with documentation
  # and verification scripts.
  set_target_properties(drawgui_multi_window PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # Needs a display, so it is a demo rather than a test: there is no CTest entry
  # for it, because a GUI entry would fail on every headless machine and the only
  # way to keep it green would be to stop asserting anything. It is built
  # wherever SDL3 is available, including CI, so it cannot rot uncompiled.
endif()
