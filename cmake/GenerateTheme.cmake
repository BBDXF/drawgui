# ============================================================================
# GenerateTheme.cmake - theme token code generation.
#
# Mirrors cmake/GenerateProps.cmake exactly, for the second numeric-id
# family this project generates (design.md section 5.7.7). See that file's
# own header comment for the reasoning; it is not repeated here.
# ============================================================================

if(DEFINED DRAWGUI_GENERATE_THEME_INCLUDED)
  return()
endif()
set(DRAWGUI_GENERATE_THEME_INCLUDED TRUE)

get_filename_component(DRAWGUI_THEME_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

set(DRAWGUI_THEME_GENERATOR "${DRAWGUI_THEME_ROOT}/tools/gen_theme.py")
set(DRAWGUI_THEME_LOCK_TOOL "${DRAWGUI_THEME_ROOT}/tools/theme_lock.py")
set(DRAWGUI_THEME_CONSISTENCY_TOOL "${DRAWGUI_THEME_ROOT}/tools/check_consistency.py")
set(DRAWGUI_THEME_TOML "${DRAWGUI_THEME_ROOT}/themes/schema.toml")
set(DRAWGUI_THEME_LOCK "${DRAWGUI_THEME_ROOT}/themes/token_ids.lock")
set(DRAWGUI_THEME_BUILTIN_JSON "${DRAWGUI_THEME_ROOT}/themes/builtin/theme.json")
set(DRAWGUI_THEME_INCLUDE_DIR "${DRAWGUI_THEME_ROOT}/include")
set(DRAWGUI_THEME_HEADER
    "${DRAWGUI_THEME_ROOT}/include/drawgui/theme/token_ids.generated.h")
set(DRAWGUI_THEME_TABLE
    "${DRAWGUI_THEME_ROOT}/src/theme/token_table.generated.inc")
set(DRAWGUI_THEME_DOC
    "${DRAWGUI_THEME_ROOT}/doc/theme-tokens.generated.md")
set(DRAWGUI_THEME_BUILTIN_HEADER
    "${DRAWGUI_THEME_ROOT}/include/drawgui/theme/builtin_theme.generated.h")
set(DRAWGUI_THEME_GENERATED_FILES
    "${DRAWGUI_THEME_HEADER}"
    "${DRAWGUI_THEME_TABLE}"
    "${DRAWGUI_THEME_DOC}"
    "${DRAWGUI_THEME_BUILTIN_HEADER}")

foreach(_drawgui_theme_input IN ITEMS
        "${DRAWGUI_THEME_GENERATOR}"
        "${DRAWGUI_THEME_LOCK_TOOL}"
        "${DRAWGUI_THEME_CONSISTENCY_TOOL}"
        "${DRAWGUI_THEME_TOML}"
        "${DRAWGUI_THEME_LOCK}"
        "${DRAWGUI_THEME_BUILTIN_JSON}")
  if(NOT EXISTS "${_drawgui_theme_input}")
    message(FATAL_ERROR "GenerateTheme.cmake: missing input ${_drawgui_theme_input}")
  endif()
endforeach()
unset(_drawgui_theme_input)

add_custom_command(
  OUTPUT ${DRAWGUI_THEME_GENERATED_FILES}
  COMMAND "${Python3_EXECUTABLE}" "${DRAWGUI_THEME_GENERATOR}"
          --root "${DRAWGUI_THEME_ROOT}"
  DEPENDS "${DRAWGUI_THEME_GENERATOR}" "${DRAWGUI_THEME_TOML}" "${DRAWGUI_THEME_BUILTIN_JSON}"
  WORKING_DIRECTORY "${DRAWGUI_THEME_ROOT}"
  COMMENT "Generating token_id constants and the theme token table from themes/schema.toml"
  VERBATIM)

add_custom_target(drawgui_theme_generate ALL DEPENDS ${DRAWGUI_THEME_GENERATED_FILES})

add_custom_target(
  drawgui_theme_check
  COMMAND "${Python3_EXECUTABLE}" "${DRAWGUI_THEME_GENERATOR}"
          --root "${DRAWGUI_THEME_ROOT}" --check
  WORKING_DIRECTORY "${DRAWGUI_THEME_ROOT}"
  COMMENT "Checking that the generated theme token files match themes/schema.toml"
  VERBATIM)

add_custom_target(
  drawgui_theme_lock_check
  COMMAND "${Python3_EXECUTABLE}" "${DRAWGUI_THEME_LOCK_TOOL}"
          --root "${DRAWGUI_THEME_ROOT}" --check
  WORKING_DIRECTORY "${DRAWGUI_THEME_ROOT}"
  COMMENT "Checking themes/schema.toml against the token id lock"
  VERBATIM)

# design.md section 5.7.7 (C4): the builtin theme must cover every schema
# token. This is the consistency gate, distinct from drawgui_theme_check
# (schema vs. generated files) and drawgui_theme_lock_check (schema vs. its
# own ABI history) - this one checks schema vs. DATA.
add_custom_target(
  drawgui_theme_consistency
  COMMAND "${Python3_EXECUTABLE}" "${DRAWGUI_THEME_CONSISTENCY_TOOL}"
  WORKING_DIRECTORY "${DRAWGUI_THEME_ROOT}"
  COMMENT "Checking that themes/builtin/theme.json covers every schema token"
  VERBATIM)

set_property(
  DIRECTORY APPEND
  PROPERTY CMAKE_CONFIGURE_DEPENDS "${DRAWGUI_THEME_TOML}" "${DRAWGUI_THEME_BUILTIN_JSON}")

set_source_files_properties(${DRAWGUI_THEME_GENERATED_FILES} PROPERTIES GENERATED TRUE)
