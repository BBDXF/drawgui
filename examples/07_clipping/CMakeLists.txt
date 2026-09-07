if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_clipping
    main.cpp
    clip_check.cpp
    clip_scene.cpp
    clip_window.cpp)

  target_link_libraries(drawgui_clipping PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_clipping PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_clipping PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-clipping needs no display, so it runs wherever this target is
  # built. It is NOT the primary gate: this target does not exist without
  # SDL3 and CI has none today, so tests/unit/test_clip.cpp and
  # tests/unit/test_clip_damage.cpp carry the same claims into an
  # always-built binary. This entry checks them against the scene a human
  # actually resizes.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME clipping.verify_demo_scene
      COMMAND drawgui_clipping --verify-clipping)
  endif()
endif()
