# ============================================================================
# GenerateProps.cmake - property code generation.
#
# The top-level CMakeLists.txt includes this fragment:
#
#     include(${CMAKE_CURRENT_LIST_DIR}/cmake/GenerateProps.cmake)
#     add_dependencies(drawgui drawgui_props_generate)
#     target_include_directories(drawgui PUBLIC ${DRAWGUI_PROPS_INCLUDE_DIR})
#
# props/drawgui.props.toml is the single source of truth (design.md section
# 5.8 decision 5). Both generated files are committed, so this fragment
# regenerates them on every build: a stale copy cannot survive a build, and
# the drawgui_props_check target turns a stale copy into a hard failure.
# ============================================================================

if(DEFINED DRAWGUI_GENERATE_PROPS_INCLUDED)
  return()
endif()
set(DRAWGUI_GENERATE_PROPS_INCLUDED TRUE)

get_filename_component(DRAWGUI_PROPS_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# tomllib landed in the Python standard library in 3.11, so the generator
# needs no third-party TOML package.
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

set(DRAWGUI_PROPS_GENERATOR "${DRAWGUI_PROPS_ROOT}/tools/gen_props.py")
set(DRAWGUI_PROPS_LOCK_TOOL "${DRAWGUI_PROPS_ROOT}/tools/prop_lock.py")
set(DRAWGUI_PROPS_LOCK_SELFTEST "${DRAWGUI_PROPS_ROOT}/tools/prop_lock_selftest.py")
set(DRAWGUI_PROPS_TOML "${DRAWGUI_PROPS_ROOT}/props/drawgui.props.toml")
set(DRAWGUI_PROPS_LOCK "${DRAWGUI_PROPS_ROOT}/props/prop_ids.lock")
set(DRAWGUI_PROPS_INCLUDE_DIR "${DRAWGUI_PROPS_ROOT}/include")
set(DRAWGUI_PROPS_HEADER
    "${DRAWGUI_PROPS_ROOT}/include/drawgui/render/prop_ids.generated.h")
set(DRAWGUI_PROPS_DISPATCH
    "${DRAWGUI_PROPS_ROOT}/src/render/prop_dispatch.generated.inc")
set(DRAWGUI_PROPS_GENERATED_FILES
    "${DRAWGUI_PROPS_HEADER}"
    "${DRAWGUI_PROPS_DISPATCH}")

foreach(_drawgui_props_input IN ITEMS
        "${DRAWGUI_PROPS_GENERATOR}"
        "${DRAWGUI_PROPS_LOCK_TOOL}"
        "${DRAWGUI_PROPS_LOCK_SELFTEST}"
        "${DRAWGUI_PROPS_TOML}"
        "${DRAWGUI_PROPS_LOCK}")
  if(NOT EXISTS "${_drawgui_props_input}")
    message(FATAL_ERROR "GenerateProps.cmake: missing input ${_drawgui_props_input}")
  endif()
endforeach()
unset(_drawgui_props_input)

add_custom_command(
  OUTPUT ${DRAWGUI_PROPS_GENERATED_FILES}
  COMMAND "${Python3_EXECUTABLE}" "${DRAWGUI_PROPS_GENERATOR}"
          --root "${DRAWGUI_PROPS_ROOT}"
  DEPENDS "${DRAWGUI_PROPS_GENERATOR}" "${DRAWGUI_PROPS_TOML}"
  WORKING_DIRECTORY "${DRAWGUI_PROPS_ROOT}"
  COMMENT "Generating prop_id constants and property dispatch from props/drawgui.props.toml"
  VERBATIM)

# Every target that includes a generated file must depend on this one.
add_custom_target(drawgui_props_generate ALL DEPENDS ${DRAWGUI_PROPS_GENERATED_FILES})

# The CI gate. It never writes, so it fails rather than papering over drift.
add_custom_target(
  drawgui_props_check
  COMMAND "${Python3_EXECUTABLE}" "${DRAWGUI_PROPS_GENERATOR}"
          --root "${DRAWGUI_PROPS_ROOT}" --check
  WORKING_DIRECTORY "${DRAWGUI_PROPS_ROOT}"
  COMMENT "Checking that the generated property files match props/drawgui.props.toml"
  VERBATIM)

# The ABI gate. drawgui_props_check only proves the generated files agree with
# the TOML; it regenerates just as happily after someone moves `width` from id
# 1 to id 7, which silently breaks every already-compiled consumer. This target
# compares the TOML against the committed props/prop_ids.lock instead, so a
# renumber, a rename or a deletion is a hard failure. Appending a new property
# is MINOR and passes (design.md section 5.8 decision 4).
add_custom_target(
  drawgui_props_lock_check
  COMMAND "${Python3_EXECUTABLE}" "${DRAWGUI_PROPS_LOCK_TOOL}"
          --root "${DRAWGUI_PROPS_ROOT}" --check
  WORKING_DIRECTORY "${DRAWGUI_PROPS_ROOT}"
  COMMENT "Checking props/drawgui.props.toml against the property id lock"
  VERBATIM)

# The gate on the gate. drawgui_props_lock_check runs the lock against a tree
# that is correct, and correct input is precisely what cannot distinguish a
# working check from one that returns 0 unconditionally - which is what the
# first version of prop_lock.py actually did for an unrecorded append. This
# target feeds the lock deliberately broken tables and requires it to reject
# every one of them.
add_custom_target(
  drawgui_props_lock_selftest
  COMMAND "${Python3_EXECUTABLE}" "${DRAWGUI_PROPS_LOCK_SELFTEST}"
  WORKING_DIRECTORY "${DRAWGUI_PROPS_ROOT}"
  COMMENT "Checking that the property id lock still rejects renumbering and unrecorded appends"
  VERBATIM)

# Re-run CMake when the source of truth changes, so a fresh property shows up
# without a manual reconfigure.
set_property(
  DIRECTORY APPEND
  PROPERTY CMAKE_CONFIGURE_DEPENDS "${DRAWGUI_PROPS_TOML}")

# The generated files are outputs of a command, not hand-maintained sources.
set_source_files_properties(${DRAWGUI_PROPS_GENERATED_FILES} PROPERTIES GENERATED TRUE)
