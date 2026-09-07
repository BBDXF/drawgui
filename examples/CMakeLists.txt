add_subdirectory(00_cpu_raster_png)

if(DRAWGUI_HAVE_SDL3)
  add_subdirectory(01_sdl3_multi_window)
  add_subdirectory(02_skia_cpu_gallery)
endif()
