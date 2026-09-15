if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_image
    main.cpp
    image_check.cpp
    image_scene.cpp
    image_window.cpp)

  target_link_libraries(drawgui_image PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_image PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_image PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-image needs no display, so it runs wherever this target is
  # built. It is NOT the primary gate: this target does not exist without
  # SDL3 and CI has none today, so tests/unit/test_image.cpp carries the
  # same claims into an always-built binary. This entry checks them against
  # the scene a human actually looks at.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME image.verify_demo_scene
      COMMAND drawgui_image --verify-image)
  endif()
endif()
