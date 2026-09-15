# ============================================================================
# GenerateAbi.cmake - C ABI code generation.
#
# Mirrors cmake/GenerateProps.cmake and cmake/GenerateTheme.cmake exactly, for
# the third generated-numeric-id family this project builds (design.md
# section 5.8 decision 5). See GenerateProps.cmake's own header comment for
# the reasoning; it is not repeated here.
#
# tools/gen_abi.py additionally imports tools/gen_props.py directly (its
# load_definitions()), so props/drawgui.props.toml is also a dependency of
# the generated ABI files - a prop_id renumbered there without touching
# abi/drawgui.def.toml at all still has to regenerate drawgui.h correctly.
# ============================================================================

if(DEFINED DRAWGUI_GENERATE_ABI_INCLUDED)
  return()
endif()
set(DRAWGUI_GENERATE_ABI_INCLUDED TRUE)

get_filename_component(DRAWGUI_ABI_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

set(DRAWGUI_ABI_GENERATOR "${DRAWGUI_ABI_ROOT}/tools/gen_abi.py")
set(DRAWGUI_ABI_LOCK_TOOL "${DRAWGUI_ABI_ROOT}/tools/abi_lock.py")
set(DRAWGUI_ABI_TOML "${DRAWGUI_ABI_ROOT}/abi/drawgui.def.toml")
set(DRAWGUI_ABI_LOCK "${DRAWGUI_ABI_ROOT}/abi/drawgui_abi.lock")
set(DRAWGUI_PROPS_TOML_FOR_ABI "${DRAWGUI_ABI_ROOT}/props/drawgui.props.toml")
set(DRAWGUI_ABI_INCLUDE_DIR "${DRAWGUI_ABI_ROOT}/include")
set(DRAWGUI_ABI_HEADER "${DRAWGUI_ABI_ROOT}/include/drawgui/abi/drawgui.h")
set(DRAWGUI_ABI_IMPL "${DRAWGUI_ABI_ROOT}/src/abi/drawgui_abi.generated.cpp")
set(DRAWGUI_ABI_DTS "${DRAWGUI_ABI_ROOT}/abi/drawgui.d.ts")
set(DRAWGUI_ABI_GENERATED_FILES
    "${DRAWGUI_ABI_HEADER}"
    "${DRAWGUI_ABI_IMPL}"
    "${DRAWGUI_ABI_DTS}")

foreach(_drawgui_abi_input IN ITEMS
        "${DRAWGUI_ABI_GENERATOR}"
        "${DRAWGUI_ABI_LOCK_TOOL}"
        "${DRAWGUI_ABI_TOML}"
        "${DRAWGUI_ABI_LOCK}"
        "${DRAWGUI_PROPS_TOML_FOR_ABI}")
  if(NOT EXISTS "${_drawgui_abi_input}")
    message(FATAL_ERROR "GenerateAbi.cmake: missing input ${_drawgui_abi_input}")
  endif()
endforeach()
unset(_drawgui_abi_input)

add_custom_command(
  OUTPUT ${DRAWGUI_ABI_GENERATED_FILES}
  COMMAND "${Python3_EXECUTABLE}" "${DRAWGUI_ABI_GENERATOR}"
          --root "${DRAWGUI_ABI_ROOT}"
  DEPENDS "${DRAWGUI_ABI_GENERATOR}" "${DRAWGUI_ABI_TOML}" "${DRAWGUI_PROPS_TOML_FOR_ABI}"
  WORKING_DIRECTORY "${DRAWGUI_ABI_ROOT}"
  COMMENT "Generating drawgui.h / drawgui_abi.generated.cpp / drawgui.d.ts from abi/drawgui.def.toml"
  VERBATIM)

add_custom_target(drawgui_abi_generate ALL DEPENDS ${DRAWGUI_ABI_GENERATED_FILES})

add_custom_target(
  drawgui_abi_check
  COMMAND "${Python3_EXECUTABLE}" "${DRAWGUI_ABI_GENERATOR}"
          --root "${DRAWGUI_ABI_ROOT}" --check
  WORKING_DIRECTORY "${DRAWGUI_ABI_ROOT}"
  COMMENT "Checking that the generated ABI files match abi/drawgui.def.toml"
  VERBATIM)

# design.md section 5.8 decision 4, mechanically enforced: a function may
# only be added, never removed or re-signed; a struct's locked fields may
# only be an unchanged PREFIX of its current ones (append-only); a constant
# may only be added, never renumbered or reused.
add_custom_target(
  drawgui_abi_lock_check
  COMMAND "${Python3_EXECUTABLE}" "${DRAWGUI_ABI_LOCK_TOOL}"
          --root "${DRAWGUI_ABI_ROOT}" --check
  WORKING_DIRECTORY "${DRAWGUI_ABI_ROOT}"
  COMMENT "Checking abi/drawgui.def.toml against the ABI stability lock"
  VERBATIM)

set_property(
  DIRECTORY APPEND
  PROPERTY CMAKE_CONFIGURE_DEPENDS "${DRAWGUI_ABI_TOML}" "${DRAWGUI_PROPS_TOML_FOR_ABI}")

set_source_files_properties(${DRAWGUI_ABI_GENERATED_FILES} PROPERTIES GENERATED TRUE)
