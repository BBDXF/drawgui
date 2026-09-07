if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_widgets
    main.cpp
    clip_probe.cpp
    demo.cpp
    hud.cpp
    widget_check.cpp
    widget_scene.cpp)

  # Skia is linked here for the readout only. Text IS a render-tree node now -
  # every label and every button caption in the scene goes through
  # dg::TextStyle - but the HUD is a formatted table that would be a dozen
  # extra nodes re-laid-out on a timer, which would make the demo measure
  # itself. When a text widget can shrink-wrap its content, hud.cpp is what
  # disappears.
  target_link_libraries(drawgui_widgets PRIVATE
    drawgui::drawgui
    drawgui::skia
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_widgets PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_widgets PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # Unlike the demo itself, --verify-widgets needs no display. Registered here
  # so that a machine with SDL3 runs the check against the binary a human
  # actually clicks.
  #
  # The primary gate is NOT this entry, because this target does not exist
  # without SDL3 and CI has none today. tests/widget/widget_identity_main.cpp
  # and tests/unit/test_widget_pipeline.cpp compile the same scene and the same
  # pointer routing into always-built binaries, so the check cannot silently
  # stop running.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME widgets.verify_demo_scene
      COMMAND drawgui_widgets --verify-widgets --size 900x680)
  endif()
endif()
