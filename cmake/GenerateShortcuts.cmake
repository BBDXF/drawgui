# ============================================================================
# GenerateShortcuts.cmake - shortcut/intent action code generation.
#
# Mirrors cmake/GenerateTheme.cmake exactly, for the fourth numeric-id
# family this project generates (design.md section 5.5.3). See that file's
# own header comment for the reasoning; it is not repeated here.
# ============================================================================

if(DEFINED DRAWGUI_GENERATE_SHORTCUTS_INCLUDED)
  return()
endif()
set(DRAWGUI_GENERATE_SHORTCUTS_INCLUDED TRUE)

get_filename_component(DRAWGUI_SHORTCUTS_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

set(DRAWGUI_SHORTCUTS_GENERATOR "${DRAWGUI_SHORTCUTS_ROOT}/tools/gen_shortcuts.py")
set(DRAWGUI_SHORTCUTS_LOCK_TOOL "${DRAWGUI_SHORTCUTS_ROOT}/tools/shortcut_lock.py")
set(DRAWGUI_SHORTCUTS_LOCK_SELFTEST "${DRAWGUI_SHORTCUTS_ROOT}/tools/shortcut_lock_selftest.py")
set(DRAWGUI_SHORTCUTS_TOML "${DRAWGUI_SHORTCUTS_ROOT}/input/shortcuts.toml")
set(DRAWGUI_SHORTCUTS_LOCK "${DRAWGUI_SHORTCUTS_ROOT}/input/action_ids.lock")
set(DRAWGUI_SHORTCUTS_INCLUDE_DIR "${DRAWGUI_SHORTCUTS_ROOT}/include")
set(DRAWGUI_SHORTCUTS_ACTION_HEADER
    "${DRAWGUI_SHORTCUTS_ROOT}/include/drawgui/shortcuts/action_ids.generated.h")
set(DRAWGUI_SHORTCUTS_LOGICAL_KEY_HEADER
    "${DRAWGUI_SHORTCUTS_ROOT}/include/drawgui/shortcuts/logical_key.generated.h")
set(DRAWGUI_SHORTCUTS_SCOPE_HEADER
    "${DRAWGUI_SHORTCUTS_ROOT}/include/drawgui/shortcuts/action_scope.generated.h")
set(DRAWGUI_SHORTCUTS_KEY_TABLE
    "${DRAWGUI_SHORTCUTS_ROOT}/src/shortcuts/logical_key_table.generated.inc")
set(DRAWGUI_SHORTCUTS_BINDING_TABLE
    "${DRAWGUI_SHORTCUTS_ROOT}/src/shortcuts/binding_table.generated.inc")
set(DRAWGUI_SHORTCUTS_DOC
    "${DRAWGUI_SHORTCUTS_ROOT}/doc/shortcuts.generated.md")
set(DRAWGUI_SHORTCUTS_GENERATED_FILES
    "${DRAWGUI_SHORTCUTS_ACTION_HEADER}"
    "${DRAWGUI_SHORTCUTS_LOGICAL_KEY_HEADER}"
    "${DRAWGUI_SHORTCUTS_SCOPE_HEADER}"
    "${DRAWGUI_SHORTCUTS_KEY_TABLE}"
    "${DRAWGUI_SHORTCUTS_BINDING_TABLE}"
    "${DRAWGUI_SHORTCUTS_DOC}")

foreach(_drawgui_shortcuts_input IN ITEMS
        "${DRAWGUI_SHORTCUTS_GENERATOR}"
        "${DRAWGUI_SHORTCUTS_LOCK_TOOL}"
        "${DRAWGUI_SHORTCUTS_LOCK_SELFTEST}"
        "${DRAWGUI_SHORTCUTS_TOML}"
        "${DRAWGUI_SHORTCUTS_LOCK}")
  if(NOT EXISTS "${_drawgui_shortcuts_input}")
    message(FATAL_ERROR "GenerateShortcuts.cmake: missing input ${_drawgui_shortcuts_input}")
  endif()
endforeach()
unset(_drawgui_shortcuts_input)

add_custom_command(
  OUTPUT ${DRAWGUI_SHORTCUTS_GENERATED_FILES}
  COMMAND "${Python3_EXECUTABLE}" "${DRAWGUI_SHORTCUTS_GENERATOR}"
          --root "${DRAWGUI_SHORTCUTS_ROOT}"
  DEPENDS "${DRAWGUI_SHORTCUTS_GENERATOR}" "${DRAWGUI_SHORTCUTS_TOML}"
  WORKING_DIRECTORY "${DRAWGUI_SHORTCUTS_ROOT}"
  COMMENT "Generating action_id/LogicalKey/ActionScope from input/shortcuts.toml"
  VERBATIM)

add_custom_target(drawgui_shortcuts_generate ALL DEPENDS ${DRAWGUI_SHORTCUTS_GENERATED_FILES})

# The CI gate. It never writes, so it fails rather than papering over drift.
add_custom_target(
  drawgui_shortcuts_check
  COMMAND "${Python3_EXECUTABLE}" "${DRAWGUI_SHORTCUTS_GENERATOR}"
          --root "${DRAWGUI_SHORTCUTS_ROOT}" --check
  WORKING_DIRECTORY "${DRAWGUI_SHORTCUTS_ROOT}"
  COMMENT "Checking that the generated shortcut files match input/shortcuts.toml"
  VERBATIM)

# The ABI gate. drawgui_shortcuts_check only proves the generated files agree
# with the TOML; it regenerates just as happily after someone moves `copy`
# from id 1 to id 5. This target compares the TOML against the committed
# input/action_ids.lock instead, so a renumber, a rename or a deletion is a
# hard failure. Appending a new action is MINOR and passes.
add_custom_target(
  drawgui_shortcuts_lock_check
  COMMAND "${Python3_EXECUTABLE}" "${DRAWGUI_SHORTCUTS_LOCK_TOOL}"
          --root "${DRAWGUI_SHORTCUTS_ROOT}" --check
  WORKING_DIRECTORY "${DRAWGUI_SHORTCUTS_ROOT}"
  COMMENT "Checking input/shortcuts.toml against the action id lock"
  VERBATIM)

# The gate on the gate - see tools/shortcut_lock_selftest.py's own docstring.
add_custom_target(
  drawgui_shortcuts_lock_selftest
  COMMAND "${Python3_EXECUTABLE}" "${DRAWGUI_SHORTCUTS_LOCK_SELFTEST}"
  WORKING_DIRECTORY "${DRAWGUI_SHORTCUTS_ROOT}"
  COMMENT "Checking that the shortcut generator/lock still reject tampering"
  VERBATIM)

set_property(
  DIRECTORY APPEND
  PROPERTY CMAKE_CONFIGURE_DEPENDS "${DRAWGUI_SHORTCUTS_TOML}")

set_source_files_properties(${DRAWGUI_SHORTCUTS_GENERATED_FILES} PROPERTIES GENERATED TRUE)
