# ============================================================================
# FetchSkia2.cmake - obtain BBDXF/libskia2 and expose it as skia2::skia2.
#
# Supersedes cmake/FetchSkia.cmake (deleted in the same commit as this file's
# introduction). The old file fetched two artifacts separately - a rust-skia
# binary release asset (pinned by SHA256) and a rust-skia/skia header
# checkout (pinned by a shallow git clone's HEAD commit) - and then hand-wrote
# link order, include paths and system libraries for the result. Both halves
# of that were the owner's problem to maintain, not upstream's; see 7-1 in
# .omo/plans/drawgui-phase7.md and doc/skia-dependency.md for the full
# rationale for switching providers.
#
# libskia2 (https://github.com/BBDXF/libskia2) is a purpose-built Skia
# distribution for exactly this kind of self-drawn GUI framework:
#
#   - ONE release tarball per platform carries everything: 7 static
#     libraries, the full header tree (already namespaced under
#     include/skia/), and a generated CMake package
#     (lib/cmake/skia2/skia2Config.cmake). There is no second artifact to
#     keep in sync - the old two-pin, two-verification-method design is gone
#     because there is only one thing to fetch and verify.
#   - The package declares link order (topological, via
#     INTERFACE_LINK_LIBRARIES - no --start-group/--end-group needed), include
#     paths, required system libraries (fontconfig + Threads + dl + m on
#     Linux) and the ABI-affecting compile options (C++20, and on Windows
#     /utf-8 plus _HAS_EXCEPTIONS=0) that a consumer would otherwise have to
#     get right by hand. Consuming it is one call:
#       find_package(skia2 REQUIRED)
#       target_link_libraries(... PRIVATE skia2::skia2)
#   - Skia version is chrome/m153, pinned to a commit in upstream's own
#     config/skia.pin (4f574af2444846ceca4d277a8095c5d4229d175f for v0.1.0),
#     which is a different commit/repo than the old rust-skia/skia fork this
#     project previously pinned by tag - not a regression, because the header
#     package now travels inside the SAME tarball as the binaries it matches,
#     so there is no longer a "do these two pins agree" question to ask.
#   - Text stack is libgrapheme, not ICU: no icudtl.dat anywhere. See
#     doc/skia-dependency.md for the settlement of design.md section 12 open
#     question 5.
#
# --- Verification design ----------------------------------------------------
#
# Upstream publishes a `.sha256` sidecar next to every release tarball - one
# thing this project's own hand-maintained "<triple>:<features>" registry
# never had, because rust-skia/skia-binaries never published one. That is
# strictly better for the common case (no PR needed to add a hash whenever
# consuming a new asset), but the sidecar comes from the same release as the
# archive: an attacker (or an accidental force-push of the release) capable
# of replacing the tarball can replace the sidecar identically, so the
# sidecar alone only defends against transport corruption, not against the
# release changing under this project's feet. TLS already covers transport
# corruption/MITM. What the sidecar does NOT cover, and what actually
# happened to this exact dependency once already (the old registry design's
# whole reason to exist), is upstream quietly moving a tag or re-cutting a
# release with different bytes.
#
# So both checks run, in this order:
#   1. The downloaded `.sha256` sidecar's hash must equal the hash pinned
#      in-repo below (DRAWGUI_SKIA2_SHA256). A mismatch here means the
#      release changed since this file was last reviewed - fatal, because an
#      unreviewed change to a linker input is exactly the supply-chain hole
#      the old registry was built to prevent, and losing that guarantee would
#      be a regression, not a simplification.
#   2. The downloaded archive's actual SHA256 must equal that same pinned
#      hash. A mismatch here means transport corruption or a sidecar/archive
#      inconsistency - also fatal.
# Only once both agree is the archive extracted. Updating the pin below is a
# reviewed, deliberate action, identical in spirit to the old registry's
# per-asset entries - there is exactly one entry now instead of one per
# "<triple>:<features>" combination, because libskia2 does not fragment its
# release by feature subset the way rust-skia's did.
# ============================================================================

if(DEFINED DRAWGUI_FETCH_SKIA2_INCLUDED)
  return()
endif()
set(DRAWGUI_FETCH_SKIA2_INCLUDED TRUE)

# --- Pins -------------------------------------------------------------------

set(DRAWGUI_SKIA2_VERSION "0.1.0"
    CACHE STRING "libskia2 release tag (without the leading v)")
set(DRAWGUI_SKIA2_REPO "https://github.com/BBDXF/libskia2"
    CACHE STRING "Repository hosting libskia2 releases")

# --- Target triple -----------------------------------------------------------

# libskia2 v0.1.0 publishes linux-x64 and windows-x64-msvc. Only linux-x64 is
# consumed by this project today (7-1's scope is Linux; design.md section 3.2
# lists Windows too, and the windows-x64-msvc tarball's existence matters for
# that goal, but wiring up an MSVC build/CI job is explicitly out of scope for
# this slice - see .omo/plans/drawgui-phase7.md 7-1).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  set(_dg_skia2_target "linux-x64")
  set(_dg_skia2_sha256 "f3d698e92deb8b4c6c627a3a3dd132a95f89a6c8d5f5d5c8c27e4d02a55abb07")
else()
  message(FATAL_ERROR
    "FetchSkia2.cmake: no libskia2 mapping for ${CMAKE_SYSTEM_NAME} / "
    "${CMAKE_SYSTEM_PROCESSOR}. Only linux-x64 is wired up in this slice; a "
    "windows-x64-msvc tarball is published upstream but its CMake consumption "
    "(and CI) is out of scope for 7-1 - see .omo/plans/drawgui-phase7.md.")
endif()

set(DRAWGUI_SKIA2_SHA256 "${_dg_skia2_sha256}"
    CACHE STRING "Expected SHA256 of the libskia2 release tarball for this platform")

set(_dg_skia2_asset "libskia2-${DRAWGUI_SKIA2_VERSION}-${_dg_skia2_target}.tar.gz")
set(_dg_skia2_url "${DRAWGUI_SKIA2_REPO}/releases/download/v${DRAWGUI_SKIA2_VERSION}/${_dg_skia2_asset}")
set(_dg_skia2_sha_url "${_dg_skia2_url}.sha256")

# --- Layout -------------------------------------------------------------------

get_filename_component(_dg_skia2_root "${CMAKE_CURRENT_LIST_DIR}/../third_party/skia-prebuilt" ABSOLUTE)
set(_dg_skia2_archive "${_dg_skia2_root}/${_dg_skia2_asset}")
set(_dg_skia2_sha_file "${_dg_skia2_archive}.sha256")
# The tarball's own top-level directory (matches its basename with the
# extension stripped) is the "prefix" find_package(skia2) is given -
# lib/cmake/skia2/skia2Config.cmake lives three levels under it.
set(_dg_skia2_prefix "${_dg_skia2_root}/libskia2-${DRAWGUI_SKIA2_VERSION}-${_dg_skia2_target}")
set(_dg_skia2_config "${_dg_skia2_prefix}/lib/cmake/skia2/skia2Config.cmake")

file(MAKE_DIRECTORY "${_dg_skia2_root}")

# --- Fetch, verify, extract ---------------------------------------------------

if(NOT EXISTS "${_dg_skia2_config}")
  # The sidecar is tiny (under 100 bytes); fetch it every time a fresh
  # extraction is needed so a moved release is caught even if a stale archive
  # happens to already be sitting on disk from a previous, different pin.
  message(STATUS "Fetching libskia2 SHA256 sidecar: ${_dg_skia2_sha_url}")
  file(DOWNLOAD "${_dg_skia2_sha_url}" "${_dg_skia2_sha_file}"
       STATUS _dg_dl_status
       TLS_VERIFY ON)
  list(GET _dg_dl_status 0 _dg_dl_code)
  if(NOT _dg_dl_code EQUAL 0)
    list(GET _dg_dl_status 1 _dg_dl_msg)
    file(REMOVE "${_dg_skia2_sha_file}")
    message(FATAL_ERROR
      "FetchSkia2.cmake: failed to download the SHA256 sidecar (${_dg_dl_code}): ${_dg_dl_msg}\n"
      "  URL: ${_dg_skia2_sha_url}")
  endif()

  # The sidecar format is `<hash>  <filename>` (sha256sum's own output
  # format), one line. Only the hash is needed.
  file(STRINGS "${_dg_skia2_sha_file}" _dg_skia2_sha_line LIMIT_COUNT 1)
  string(REGEX MATCH "^[0-9a-fA-F]+" _dg_skia2_sidecar_sha "${_dg_skia2_sha_line}")
  string(TOLOWER "${_dg_skia2_sidecar_sha}" _dg_skia2_sidecar_sha)
  string(TOLOWER "${DRAWGUI_SKIA2_SHA256}" _dg_skia2_pinned_sha)

  if(NOT _dg_skia2_sidecar_sha STREQUAL _dg_skia2_pinned_sha)
    file(REMOVE "${_dg_skia2_sha_file}")
    message(FATAL_ERROR
      "FetchSkia2.cmake: the published .sha256 sidecar for ${_dg_skia2_asset} "
      "no longer matches the hash pinned in cmake/FetchSkia2.cmake.\n"
      "  pinned:   ${DRAWGUI_SKIA2_SHA256}\n"
      "  sidecar:  ${_dg_skia2_sidecar_sha}\n"
      "This means the upstream release changed since this pin was reviewed - "
      "a moved tag, a re-cut release, or tampering. Verify the new release "
      "deliberately (re-download by hand, inspect it, run its own smoke "
      "tests) before updating DRAWGUI_SKIA2_SHA256 to match. Do not update "
      "the pin to silence this error without doing that.")
  endif()

  message(STATUS "Downloading libskia2: ${_dg_skia2_asset}")
  file(DOWNLOAD "${_dg_skia2_url}" "${_dg_skia2_archive}"
       SHOW_PROGRESS
       STATUS _dg_dl_status
       TLS_VERIFY ON)
  list(GET _dg_dl_status 0 _dg_dl_code)
  if(NOT _dg_dl_code EQUAL 0)
    list(GET _dg_dl_status 1 _dg_dl_msg)
    file(REMOVE "${_dg_skia2_archive}")
    message(FATAL_ERROR
      "FetchSkia2.cmake: download failed (${_dg_dl_code}): ${_dg_dl_msg}\n"
      "  URL: ${_dg_skia2_url}")
  endif()

  file(SHA256 "${_dg_skia2_archive}" _dg_skia2_actual_sha)
  string(TOLOWER "${_dg_skia2_actual_sha}" _dg_skia2_actual_sha)
  if(NOT _dg_skia2_actual_sha STREQUAL _dg_skia2_pinned_sha)
    file(REMOVE "${_dg_skia2_archive}")
    message(FATAL_ERROR
      "FetchSkia2.cmake: SHA256 mismatch for ${_dg_skia2_asset}\n"
      "  expected: ${DRAWGUI_SKIA2_SHA256}\n"
      "  actual:   ${_dg_skia2_actual_sha}\n"
      "The corrupt archive has been deleted. Do not consume a linker input "
      "that fails its own verification.")
  endif()

  message(STATUS "Extracting libskia2 to ${_dg_skia2_root}")
  file(ARCHIVE_EXTRACT INPUT "${_dg_skia2_archive}" DESTINATION "${_dg_skia2_root}")

  if(NOT EXISTS "${_dg_skia2_config}")
    message(FATAL_ERROR
      "FetchSkia2.cmake: ${_dg_skia2_asset} extracted but "
      "${_dg_skia2_config} is missing. The tarball's internal layout may "
      "have changed upstream.")
  endif()
endif()

# --- Consume the package -------------------------------------------------------

# NO_DEFAULT_PATH: this is the one and only place skia2 should ever be found
# from. Falling back to a system-installed skia2 (there is no such thing
# today, but CMake's default search would try) would silently pick up a
# different build with different capabilities/ABI than the one this project
# verified.
find_package(skia2 REQUIRED CONFIG
  PATHS "${_dg_skia2_prefix}"
  NO_DEFAULT_PATH)

if(NOT TARGET skia2::skia2)
  message(FATAL_ERROR
    "FetchSkia2.cmake: find_package(skia2) succeeded but skia2::skia2 was "
    "not defined by ${_dg_skia2_config}.")
endif()

message(STATUS "Skia: libskia2 ${SKIA2_VERSION} (${SKIA2_TARGET}), chrome/m153, libgrapheme text stack")
