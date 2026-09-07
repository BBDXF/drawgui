add_executable(drawgui_render_png main.cpp)

target_link_libraries(drawgui_render_png PRIVATE
  drawgui::drawgui
  drawgui_warnings
  drawgui_sanitizers)

# Keep binaries in build/examples/ for backwards compatibility with documentation
# and verification scripts.
set_target_properties(drawgui_render_png PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")
