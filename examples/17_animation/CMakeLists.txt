if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_animation
    main.cpp
    anim_check.cpp
    anim_scene.cpp
    anim_window.cpp)

  target_link_libraries(drawgui_animation PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_animation PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_animation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-animation needs no display, matching every prior
  # *.verify_demo_scene entry: this target does not exist without SDL3 (its
  # window half links WindowManager), but the check itself only touches
  # dg::AnimationEngine and dg::LayoutTree, neither of which opens a window.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME animation.verify_demo_scene
      COMMAND drawgui_animation --verify-animation)
  endif()
endif()
