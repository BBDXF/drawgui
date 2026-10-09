# The C++ runtime (src/app, include/drawgui/app) must not depend on the C
# ABI layered on top of it: no drawgui.h include, no dg_* handle or dg_event
# in its code. Run as `cmake -DROOT=<repo> -P CheckAppNoAbi.cmake`.

file(GLOB_RECURSE app_files
  "${ROOT}/src/app/*.h" "${ROOT}/src/app/*.cpp" "${ROOT}/include/drawgui/app/*.h")
if(NOT app_files)
  message(FATAL_ERROR "no runtime files found under ${ROOT}")
endif()

set(failures "")
foreach(path IN LISTS app_files)
  file(STRINGS "${path}" lines)
  set(line_no 0)
  foreach(line IN LISTS lines)
    math(EXPR line_no "${line_no} + 1")
    string(REGEX REPLACE "//.*$" "" code "${line}")
    if(code MATCHES "drawgui/abi/|dg_event|dg_app_t|dg_window_t|dg_node_t|dg_theme_t")
      string(APPEND failures "\n  ${path}:${line_no}: ${line}")
    endif()
  endforeach()
endforeach()

if(failures)
  message(FATAL_ERROR "src/app must not use the C ABI:${failures}")
endif()
