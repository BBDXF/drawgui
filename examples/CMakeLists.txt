add_executable(drawgui_render_png render_png.cpp)

target_link_libraries(drawgui_render_png PRIVATE
  drawgui::drawgui
  drawgui_warnings
  drawgui_sanitizers)
