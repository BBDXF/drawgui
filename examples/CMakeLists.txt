add_executable(drawgui_render_png render_png.cpp)

target_link_libraries(drawgui_render_png PRIVATE
  drawgui::drawgui
  drawgui_warnings
  drawgui_sanitizers)

# Needs a display, so it is a demo rather than a test: there is no CTest entry
# for it, because a GUI entry would fail on every headless machine and the only
# way to keep it green would be to stop asserting anything. It is built
# wherever SDL3 is available, including CI, so it cannot rot uncompiled.
if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_multi_window multi_window.cpp)

  target_link_libraries(drawgui_multi_window PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)
endif()
