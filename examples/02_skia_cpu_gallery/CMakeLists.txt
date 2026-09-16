if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_skia_cpu_gallery
    main.cpp
    bench.cpp
    gallery.cpp
    resources.cpp
    panels_geometry.cpp
    panels_text.cpp
    panels_effects.cpp)

  # Skia is linked here and nowhere else among the examples. This one draws
  # with Skia directly because its subject IS Skia: it exists to show what the
  # rasterizer can do and to time it, so that step 3 can design layout and
  # widgets against measured costs instead of guesses. dg::Canvas stays small
  # and grows one operation at a time when a render object needs it - writing
  # forty wrapper methods here would shape the drawing API around "what Skia
  # offers" rather than around what a widget needs, which is the mistake that
  # produced twelve unused platform headers.
  target_link_libraries(drawgui_skia_cpu_gallery PRIVATE
    drawgui::drawgui
    skia2::skia2
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_skia_cpu_gallery PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_skia_cpu_gallery PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # No CTest entry, for the same reason examples/01_sdl3_multi_window has
  # none: it needs a display. --dump-png and --bench do run headless, but
  # neither belongs in the golden suite - see doc/cpu-raster-findings.md for
  # why the gallery is deliberately not a golden baseline.
endif()
