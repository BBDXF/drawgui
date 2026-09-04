# ============================================================================
# FetchDoctest.cmake - obtain doctest and expose it as drawgui::doctest.
#
# design.md section 7 names doctest as the unit test framework. This file
# follows the same supply-chain discipline as cmake/FetchSkia.cmake: nothing
# is consumed without hash verification, and a mismatch deletes the artifact
# and hard-errors rather than building against something unexplained.
#
# Only the single header is fetched, not the release source archive. The two
# were compared and the blob served at the tag is byte-identical to the one
# inside the immutable release asset, so the header costs 363 KB instead of
# 2.2 MB, needs no extraction, and brings none of doctest's own CMake, Bazel
# or CI configuration into this build. A moved upstream tag cannot go
# unnoticed either way, because the SHA256 is checked on every configure and
# not just on first download - a header tampered with after it was cached
# would otherwise survive forever.
# ============================================================================

if(DEFINED DRAWGUI_FETCH_DOCTEST_INCLUDED)
  return()
endif()
set(DRAWGUI_FETCH_DOCTEST_INCLUDED TRUE)

# --- Pins -------------------------------------------------------------------

set(DRAWGUI_DOCTEST_VERSION "2.5.3"
    CACHE STRING "doctest release tag to consume, without the leading v")

# Computed from the actual download, and cross-checked against
# doctest/doctest.h inside the doctest-v2.5.3.tar.gz release asset.
set(DRAWGUI_DOCTEST_SHA256 "cfd518a3ef90f67e1f3ba514df23fb3627437de1a2feeba78cf5062a40021421"
    CACHE STRING "Expected SHA256 of doctest.h at DRAWGUI_DOCTEST_VERSION")

set(DRAWGUI_DOCTEST_URL
    "https://raw.githubusercontent.com/doctest/doctest/v${DRAWGUI_DOCTEST_VERSION}/doctest/doctest.h"
    CACHE STRING "Where doctest.h is fetched from")

# --- Layout -----------------------------------------------------------------

# Versioned, like the Skia header directory, so that changing the pin fetches
# afresh instead of reusing whatever happens to be on disk.
get_filename_component(_dg_doctest_root
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/doctest-${DRAWGUI_DOCTEST_VERSION}" ABSOLUTE)

# The subdirectory makes the include spelling `<doctest/doctest.h>`, which is
# what doctest's own documentation and every other project use.
set(_dg_doctest_header "${_dg_doctest_root}/doctest/doctest.h")

# --- Fetch ------------------------------------------------------------------

if(NOT EXISTS "${_dg_doctest_header}")
  file(MAKE_DIRECTORY "${_dg_doctest_root}/doctest")
  message(STATUS "Downloading doctest ${DRAWGUI_DOCTEST_VERSION}")
  file(DOWNLOAD "${DRAWGUI_DOCTEST_URL}" "${_dg_doctest_header}"
       STATUS _dg_doctest_dl_status
       TLS_VERIFY ON)
  list(GET _dg_doctest_dl_status 0 _dg_doctest_dl_code)
  if(NOT _dg_doctest_dl_code EQUAL 0)
    list(GET _dg_doctest_dl_status 1 _dg_doctest_dl_msg)
    file(REMOVE "${_dg_doctest_header}")
    message(FATAL_ERROR
      "FetchDoctest.cmake: could not download doctest ${DRAWGUI_DOCTEST_VERSION} "
      "(${_dg_doctest_dl_code}): ${_dg_doctest_dl_msg}\n"
      "  URL: ${DRAWGUI_DOCTEST_URL}\n"
      "This build needs network access once. To build without it, fetch the "
      "header on a connected machine and drop it in place - it is verified by "
      "SHA256 either way:\n"
      "  curl -L -o ${_dg_doctest_header} ${DRAWGUI_DOCTEST_URL}\n"
      "Or configure with -DDRAWGUI_BUILD_TESTS=OFF to build without the test "
      "suite.")
  endif()
endif()

# Verified on every configure, not only after a download: the point of the
# hash is to describe what is on disk right now.
file(SHA256 "${_dg_doctest_header}" _dg_doctest_actual_sha)
if(NOT _dg_doctest_actual_sha STREQUAL DRAWGUI_DOCTEST_SHA256)
  file(REMOVE "${_dg_doctest_header}")
  message(FATAL_ERROR
    "FetchDoctest.cmake: SHA256 mismatch for doctest.h\n"
    "  expected: ${DRAWGUI_DOCTEST_SHA256}\n"
    "  actual:   ${_dg_doctest_actual_sha}\n"
    "  URL:      ${DRAWGUI_DOCTEST_URL}\n"
    "The unverified header has been deleted, so the next configure fetches "
    "again. If the pin is genuinely being moved, change both "
    "DRAWGUI_DOCTEST_VERSION and DRAWGUI_DOCTEST_SHA256 in "
    "cmake/FetchDoctest.cmake deliberately.")
endif()

# --- The target -------------------------------------------------------------

add_library(drawgui_doctest INTERFACE)

# SYSTEM, for the same reason drawgui::skia is SYSTEM. This project builds
# with -Werror plus -Wold-style-cast, -Wconversion, -Wsign-conversion and
# friends; a 360 KB third-party header does not survive that, and making it
# survive is not this project's job.
target_include_directories(drawgui_doctest SYSTEM INTERFACE "${_dg_doctest_root}")

add_library(drawgui::doctest ALIAS drawgui_doctest)

message(STATUS "doctest: ${DRAWGUI_DOCTEST_VERSION} (single header, SHA256 verified)")
