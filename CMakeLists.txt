# ============================================================================
# drawgui - a lightweight self-drawn GUI kernel.
#
# Current phase: P0 (design.md section 9) - build system, CPU raster path,
# and the golden-image pipeline. No GPU, text, windowing, layout, widgets,
# theming, C ABI or JS yet; those are P1 and later.
# ============================================================================

cmake_minimum_required(VERSION 3.24)

project(drawgui
  VERSION 0.1.0
  DESCRIPTION "A self-drawn GUI kernel embeddable through a C ABI"
  LANGUAGES CXX)

# design.md section 3.1: C++20 is the baseline, chosen for cross-platform
# determinism. C++23 library features are deliberately unused - Android NDK
# and Xcode libc++ support for them is uneven, and this project would find
# out at the worst possible moment.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DRAWGUI_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(DRAWGUI_IS_TOP_LEVEL ON)
endif()

option(DRAWGUI_BUILD_TESTS "Build the drawgui test suite" ${DRAWGUI_IS_TOP_LEVEL})
option(DRAWGUI_BUILD_EXAMPLES "Build the drawgui examples" ${DRAWGUI_IS_TOP_LEVEL})
option(DRAWGUI_WERROR "Treat compiler warnings as errors" ON)

# Always emit compile_commands.json: clang-tidy and editor tooling need it,
# and design.md section 7 puts the linter in CI from the start (constraint C4).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(${CMAKE_CURRENT_LIST_DIR}/cmake/GenerateProps.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/FetchSkia.cmake)

# --- Warning configuration --------------------------------------------------

# An interface target rather than global flags, so that only first-party code
# is held to this standard. Skia's headers are attached as SYSTEM includes and
# are exempt - GCC warns about clang's `reinitializes` attribute in
# SkTemplates.h, which is not something this project can fix.
add_library(drawgui_warnings INTERFACE)
if(MSVC)
  target_compile_options(drawgui_warnings INTERFACE /W4 /permissive-)
  if(DRAWGUI_WERROR)
    target_compile_options(drawgui_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(drawgui_warnings INTERFACE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
    -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion
    -Wdouble-promotion)
  if(DRAWGUI_WERROR)
    target_compile_options(drawgui_warnings INTERFACE -Werror)
  endif()
endif()

# --- The library ------------------------------------------------------------

add_library(drawgui STATIC
  src/graphics/canvas.cpp
  src/graphics/raster_surface.cpp)

add_library(drawgui::drawgui ALIAS drawgui)

target_include_directories(drawgui
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src)

# PRIVATE: Skia is an implementation detail. design.md section 5.3 keeps Skia
# types out of layer 3 and above, and linking it PUBLIC would let every
# consumer include Skia headers, which is precisely what that rule forbids.
target_link_libraries(drawgui
  PRIVATE
    drawgui::skia
    drawgui_warnings)

# The generated property header is committed but regenerated every build, so
# a stale copy cannot survive. See cmake/GenerateProps.cmake.
add_dependencies(drawgui drawgui_props_generate)

set_target_properties(drawgui PROPERTIES
  POSITION_INDEPENDENT_CODE ON)

# --- Tests and examples -----------------------------------------------------

if(DRAWGUI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(DRAWGUI_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
