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

# design.md section 7 (constraint C4): ASan + UBSan run the test suite. OFF by
# default - the instrumented build is roughly 2x slower and its binaries are
# not what ships, so it is a deliberate extra run rather than the normal one.
option(DG_SANITIZE "Build first-party code with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

# Always emit compile_commands.json: clang-tidy and editor tooling need it,
# and design.md section 7 puts the linter in CI from the start (constraint C4).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(${CMAKE_CURRENT_LIST_DIR}/cmake/GenerateProps.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/GenerateTheme.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/GenerateAbi.cmake)
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

# --- Sanitizer configuration ------------------------------------------------

# design.md section 7 requires ASan + UBSan over the test suite. This is a
# second interface target rather than more flags on drawgui_warnings, because
# the two do different jobs: warnings only change diagnostics, whereas these
# change the emitted code, add a runtime, and - the part that is easy to get
# wrong - must appear on the LINK line as well as the compile line. A target
# compiled with -fsanitize=address but linked without it fails on undefined
# __asan_* symbols, which reads as a mysterious linker error rather than as
# the misconfiguration it is. Hence both target_compile_options and
# target_link_options below, from one list, so they cannot drift.
#
# Like drawgui_warnings it is attached per-target rather than set globally:
# Skia is a prebuilt archive with no instrumentation, and only first-party
# code is being held to this standard. ASan is designed to link against
# uninstrumented code, so the mix works; tests/CMakeLists.txt records what was
# measured about the one boundary where it is known to need help.
#
# The target always exists so that every first-party target can link it
# unconditionally; when DG_SANITIZE is OFF it carries nothing at all.
add_library(drawgui_sanitizers INTERFACE)

if(DG_SANITIZE)
  if(MSVC)
    # /fsanitize=address exists on MSVC but there is no UBSan, so the half
    # this build would get is not what section 7 asks for. Fail loudly rather
    # than quietly delivering a weaker check under the same option name.
    message(FATAL_ERROR
      "DG_SANITIZE is not supported under MSVC: MSVC has no UndefinedBehaviorSanitizer, "
      "and an ASan-only build under this option name would misrepresent what ran.")
  endif()

  set(_dg_sanitize_flags
    -fsanitize=address
    -fsanitize=undefined
    # The load-bearing flag. Without it UBSan prints its diagnostic and lets
    # the program carry on, the test exits 0, and CI stays green over code
    # that is undefined. design.md sections 5.4.7 and 5.17.5 both choose loud
    # failure over quiet wrongness; a sanitizer that only warns is the
    # opposite of that.
    -fno-sanitize-recover=undefined
    # The one sub-check that is switched off, and it is switched off because
    # it cannot work here rather than because it is noisy. -fsanitize=vptr
    # emits a `typeinfo for T` reference at every polymorphic dereference; the
    # prebuilt Skia archive is compiled without RTTI, so `SkCanvas` has no
    # typeinfo and the sanitized link fails outright with
    # `undefined reference to 'typeinfo for SkCanvas'`. First-party code
    # declares no virtual function, no dynamic_cast and no typeid anywhere
    # (design.md section 5.15.3 stores render-object state as plain fields),
    # so this check has nothing of ours to inspect in the first place. Revisit
    # if drawgui ever grows a polymorphic type of its own.
    -fno-sanitize=vptr
    # Without a frame pointer the reports still arrive, but their stack traces
    # degrade to a handful of frames and stop naming the caller that matters.
    -fno-omit-frame-pointer
    # Line numbers in those traces, independent of CMAKE_BUILD_TYPE. A
    # sanitized Release run is worth having and is useless without this.
    -g)

  target_compile_options(drawgui_sanitizers INTERFACE ${_dg_sanitize_flags})
  target_link_options(drawgui_sanitizers INTERFACE ${_dg_sanitize_flags})

  message(STATUS "Sanitizers: ASan + UBSan (non-recovering) on first-party code")
endif()

# --- SDL3 -------------------------------------------------------------------

# Discovered rather than required. src/platform/sdl3/ and examples/01_sdl3_multi_window
# are its only consumers, and a host without libsdl3-dev should still be able
# to build and test the CPU raster path and the golden images.
#
# This is not the "a check that passes by having nothing to check" trap: a
# target either builds or does not, and there is no test entry whose verdict
# could be faked by its absence. The status line below says which way it went,
# on every configure, so a missing SDL3 is visible rather than inferred.
find_package(PkgConfig QUIET)

set(DRAWGUI_HAVE_SDL3 OFF)
if(PkgConfig_FOUND)
  pkg_check_modules(DRAWGUI_SDL3 QUIET IMPORTED_TARGET sdl3)
  if(DRAWGUI_SDL3_FOUND)
    set(DRAWGUI_HAVE_SDL3 ON)
  endif()
endif()

if(DRAWGUI_HAVE_SDL3)
  message(STATUS
    "SDL3 ${DRAWGUI_SDL3_VERSION}: building the multi-window backend and examples/01_sdl3_multi_window")
else()
  message(STATUS
    "SDL3 not found (pkg-config sdl3): skipping the multi-window backend and "
    "examples/01_sdl3_multi_window")
endif()

# --- The library ------------------------------------------------------------

add_library(drawgui STATIC
  src/anim/animation_engine.cpp
  src/anim/clock.cpp
  src/anim/curve.cpp
  src/graphics/canvas.cpp
  src/graphics/raster_surface.cpp
  src/layout/box_layout.cpp
  src/layout/layout_tree.cpp
  src/props/node_props.cpp
  src/render/clip_shape.cpp
  src/render/damage.cpp
  src/render/font_catalog.cpp
  src/render/font_fallback.cpp
  src/render/hit_test.cpp
  src/render/image_catalog.cpp
  src/render/render_tree.cpp
  src/render/skia_paint.cpp
  src/render/text_metrics.cpp
  src/render/tree_paint.cpp
  src/theme/mini_json.cpp
  src/theme/theme.cpp
  src/theme/theme_bindings.cpp
  src/theme/theme_loader.cpp
  src/widget/focus.cpp
  src/widget/interaction.cpp
  src/widget/widget_set.cpp)

if(DRAWGUI_HAVE_SDL3)
  target_sources(drawgui PRIVATE
    src/platform/sdl3/window_manager.cpp
    src/window/popup_host.cpp

    # The C ABI (design.md section 5.8): dg_app_t wraps dg::WindowManager,
    # so nothing here can exist without a real windowing backend. A build
    # without SDL3 has no C ABI at all - see doc/abi.md's own note on this.
    src/abi/abi_impl.cpp
    src/abi/drawgui_abi.generated.cpp)

  # PRIVATE for the same reason Skia is: include/drawgui/window/window_manager.h
  # names no SDL type, and linking SDL3 PUBLIC would let every consumer include
  # SDL headers, which is exactly what that header exists to prevent. A static
  # library still forwards the link requirement itself, so the demo links.
  target_link_libraries(drawgui PRIVATE PkgConfig::DRAWGUI_SDL3)
endif()

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
    drawgui_warnings
    drawgui_sanitizers)

# The generated property header is committed but regenerated every build, so
# a stale copy cannot survive. See cmake/GenerateProps.cmake.
add_dependencies(drawgui drawgui_props_generate)

# Likewise for the theme token header - see cmake/GenerateTheme.cmake.
add_dependencies(drawgui drawgui_theme_generate)

# Likewise for drawgui.h / drawgui_abi.generated.cpp - see cmake/GenerateAbi.cmake.
add_dependencies(drawgui drawgui_abi_generate)

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
