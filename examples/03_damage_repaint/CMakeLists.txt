if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_damage_repaint
    main.cpp
    demo.cpp
    hud.cpp
    scene.cpp
    selfcheck.cpp)

  # Skia is linked here for one reason only: the on-screen readout draws text,
  # and text is not a render-tree node yet. Everything else - the tree, the
  # damage, the partial repaint, the surface, the blit - goes through
  # drawgui's own API. When text becomes a node, hud.cpp is what disappears.
  target_link_libraries(drawgui_damage_repaint PRIVATE
    drawgui::drawgui
    drawgui::skia
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_damage_repaint PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_damage_repaint PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # Unlike the demo itself, --verify-damage needs no display: it renders the
  # scene twice offscreen and compares the framebuffers byte for byte. It is
  # registered here so that a machine with SDL3 runs it against the binary a
  # human actually watches.
  #
  # The primary gate is NOT this entry, because this target does not exist
  # without SDL3 and CI has none today. tests/unit/test_damage_repaint.cpp
  # compiles scene.cpp into the always-built unit test and runs the same
  # comparison there, so the check cannot silently stop running.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME damage.verify_demo_scene
      COMMAND drawgui_damage_repaint --verify-damage 240 --size 960x640)
  endif()
endif()
