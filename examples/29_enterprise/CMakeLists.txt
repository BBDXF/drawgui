if(DRAWGUI_HAVE_SDL3)
  add_executable(drawgui_enterprise
    main.cpp
    inventory.cpp
    inventory_check.cpp)

  target_link_libraries(drawgui_enterprise PRIVATE
    drawgui::drawgui
    drawgui_warnings
    drawgui_sanitizers)

  target_include_directories(drawgui_enterprise PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../common")

  set_target_properties(drawgui_enterprise PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

  # --verify runs on SDL's dummy video driver and drives the app with
  # injected pointer, key and text input: no display needed.
  if(DRAWGUI_BUILD_TESTS)
    add_test(NAME enterprise.verify
      COMMAND drawgui_enterprise --verify)
  endif()
endif()
