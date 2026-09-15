if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_popup
    main.cpp
    popup_check.cpp
    popup_scene.cpp
    popup_window.cpp)

  target_link_libraries(drawgui_popup PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_popup PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")

  set_target_properties(drawgui_popup PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify-popup needs no display: it sets SDL_VIDEODRIVER=dummy itself
  # before touching WindowManager, which is enough to open ordinary windows
  # (measured) but not real popup windows (also measured) - see
  # popup_check.cpp's own top comment and doc/popup.md for what that gap
  # means and how it is reported rather than silently skipped.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME popup.verify_demo_scene
      COMMAND drawgui_popup --verify-popup)
  endif()
endif()
