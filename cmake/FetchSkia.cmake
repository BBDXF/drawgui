# ============================================================================
# FetchSkia.cmake - obtain a prebuilt Skia and expose it as drawgui::skia.
#
# design.md section 3.2: phase one consumes prebuilt binaries rather than
# entering the GN / depot_tools swamp. The provider can be swapped later
# without changing this file's interface - callers only ever see the
# drawgui::skia target.
#
# Two artifacts are fetched, and they are verified by different means because
# they have different stability guarantees:
#
#   1. The static library, from a rust-skia/skia-binaries release asset.
#      Release assets are immutable, so this is pinned by SHA256.
#
#   2. The headers, from the rust-skia/skia repository at a tag.
#      GitHub's auto-generated source tarballs are NOT byte-stable - their
#      compression has changed before, which silently breaks a pinned hash.
#      So the headers come from a shallow git checkout whose HEAD commit is
#      verified against an expected SHA instead. A commit SHA is a stronger
#      identity claim than a tarball hash anyway.
#
# The binary and the headers must come from the same Skia revision. Mixing
# them produces a link that succeeds and then misbehaves at runtime, because
# inline functions in the headers get compiled against a different struct
# layout than the one inside the archive. Hence both pins live here, next to
# each other, and both are checked.
# ============================================================================

if(DEFINED DRAWGUI_FETCH_SKIA_INCLUDED)
  return()
endif()
set(DRAWGUI_FETCH_SKIA_INCLUDED TRUE)

# --- Pins -------------------------------------------------------------------

set(DRAWGUI_SKIA_RELEASE "0.153.2"
    CACHE STRING "rust-skia/skia-binaries release tag")
set(DRAWGUI_SKIA_BUILD_KEY "b0260d93e48425b4b39f"
    CACHE STRING "rust-skia build key embedded in the release asset name")

# design.md section 3.2 lists textlayout + svg + webp + gl as the eventual
# feature set, but they arrive one phase at a time. P1 needs Ganesh GL and
# nothing else, so this is the smallest published asset that carries it:
# textlayout would drag in HarfBuzz + ICU and the icudtl.dat distribution
# problem that design.md section 5.10.5 defers to P3. The `-x11` sibling is
# deliberately not used - it only adds Skia's own GLX native-interface
# assembly, and the GL entry points are supplied by the windowing layer's
# proc loader instead, so nothing links against X11 here.
# Switching this string is how P3 pulls in textlayout; each new value needs
# its SHA256 registered below.
set(DRAWGUI_SKIA_FEATURES "ganesh-gl-jpegd-jpege-pdf"
    CACHE STRING "Feature suffix of the rust-skia binary asset to consume")

set(DRAWGUI_SKIA_HEADER_TAG "m153-0.101.1"
    CACHE STRING "Tag in rust-skia/skia matching the binary release")
# Note: the release is named 0.153.2 after the rust-skia *crate* version. The
# skia fork tag is unrelated to that number, and a decoy tag `m154-0.153.2`
# exists. The commit below is what skia-bindings/skia actually points at, and
# is the authority - the tag is a convenience.
set(DRAWGUI_SKIA_HEADER_COMMIT "c9c3c5a91e9d74181a2ca34de81d78fef9b4d2b6"
    CACHE STRING "Expected HEAD commit of the fetched Skia headers")

set(DRAWGUI_SKIA_BINARIES_REPO "https://github.com/rust-skia/skia-binaries"
    CACHE STRING "Repository hosting the prebuilt Skia release assets")
set(DRAWGUI_SKIA_HEADERS_REPO "https://github.com/rust-skia/skia.git"
    CACHE STRING "Repository holding the Skia headers")

# --- Target triple ----------------------------------------------------------

# design.md section 3.2 lists five target triples. The current phase covers
# Linux and Windows desktop only; the rest fail loudly rather than silently
# downloading something wrong.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  set(_dg_skia_triple "x86_64-unknown-linux-gnu")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(_dg_skia_triple "aarch64-unknown-linux-gnu")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_dg_skia_triple "x86_64-pc-windows-msvc")
else()
  message(FATAL_ERROR
    "FetchSkia.cmake: no prebuilt Skia mapping for ${CMAKE_SYSTEM_NAME} / "
    "${CMAKE_SYSTEM_PROCESSOR}. Supported in this phase: x86_64 and aarch64 "
    "Linux, x86_64 Windows. See design.md section 3.2 for the full triple list.")
endif()

# --- Asset SHA256 registry --------------------------------------------------

# Keyed by "<triple>:<features>". Adding a feature set means adding its hash
# here; an unregistered combination is a hard error rather than an unverified
# download, because an unverified download of a linker input is a supply chain
# hole.
set(_dg_skia_sha256_x86_64-unknown-linux-gnu:jpegd-jpege-pdf
    "19f9ada302c9828d7a284efb62e5f0c4954dc43a9930094deec9bbc7a32cf747")
set(_dg_skia_sha256_x86_64-unknown-linux-gnu:ganesh-gl-jpegd-jpege-pdf
    "e85d9317a2b312426ab620f09a5ebe9c691ff3218abeeb352060168bfc8afe51")

set(_dg_skia_asset_key "${_dg_skia_triple}:${DRAWGUI_SKIA_FEATURES}")
if(NOT DEFINED _dg_skia_sha256_${_dg_skia_asset_key})
  message(FATAL_ERROR
    "FetchSkia.cmake: no SHA256 registered for asset '${_dg_skia_asset_key}'.\n"
    "Download the asset, run `sha256sum` on it, and register the result in "
    "cmake/FetchSkia.cmake next to the existing entries. Downloading a linker "
    "input without verifying it is not an option.")
endif()
set(_dg_skia_sha256 "${_dg_skia_sha256_${_dg_skia_asset_key}}")

set(_dg_skia_asset
    "skia-binaries-${DRAWGUI_SKIA_BUILD_KEY}-${_dg_skia_triple}-${DRAWGUI_SKIA_FEATURES}.tar.gz")
set(_dg_skia_url
    "${DRAWGUI_SKIA_BINARIES_REPO}/releases/download/${DRAWGUI_SKIA_RELEASE}/${_dg_skia_asset}")

# --- Layout -----------------------------------------------------------------

get_filename_component(_dg_skia_root "${CMAKE_CURRENT_LIST_DIR}/../third_party/skia-prebuilt" ABSOLUTE)
# The asset key uses ':' as its separator, which is not a legal path character
# on Windows. design.md section 3.2 includes x86_64-pc-windows-msvc, so the
# directory name uses '_' instead.
string(REPLACE ":" "_" _dg_skia_asset_dirname "${_dg_skia_asset_key}")
set(_dg_skia_lib_dir "${_dg_skia_root}/${_dg_skia_asset_dirname}")
set(_dg_skia_include_dir "${_dg_skia_root}/include-${DRAWGUI_SKIA_HEADER_TAG}")
set(_dg_skia_archive "${_dg_skia_root}/${_dg_skia_asset}")

file(MAKE_DIRECTORY "${_dg_skia_root}")

# --- Fetch the static library -----------------------------------------------

# rust-skia ships libskia.a next to libskia-bindings.a. The latter is the Rust
# FFI shim; linking it into a C++ program pulls in 60 undefined Rust symbols.
# Only libskia.a is ever referenced below.
set(_dg_skia_library "${_dg_skia_lib_dir}/skia-binaries/libskia.a")

if(NOT EXISTS "${_dg_skia_library}")
  if(NOT EXISTS "${_dg_skia_archive}")
    message(STATUS "Downloading Skia binaries: ${_dg_skia_asset}")
    file(DOWNLOAD "${_dg_skia_url}" "${_dg_skia_archive}"
         SHOW_PROGRESS
         STATUS _dg_dl_status
         TLS_VERIFY ON)
    list(GET _dg_dl_status 0 _dg_dl_code)
    if(NOT _dg_dl_code EQUAL 0)
      list(GET _dg_dl_status 1 _dg_dl_msg)
      file(REMOVE "${_dg_skia_archive}")
      message(FATAL_ERROR
        "FetchSkia.cmake: download failed (${_dg_dl_code}): ${_dg_dl_msg}\n"
        "  URL: ${_dg_skia_url}")
    endif()
  endif()

  file(SHA256 "${_dg_skia_archive}" _dg_skia_actual_sha)
  if(NOT _dg_skia_actual_sha STREQUAL _dg_skia_sha256)
    # Remove it: a mismatched archive left on disk would be picked up as a
    # cache hit on the next configure and never re-verified.
    file(REMOVE "${_dg_skia_archive}")
    message(FATAL_ERROR
      "FetchSkia.cmake: SHA256 mismatch for ${_dg_skia_asset}\n"
      "  expected: ${_dg_skia_sha256}\n"
      "  actual:   ${_dg_skia_actual_sha}\n"
      "The corrupt archive has been deleted. If the upstream asset legitimately "
      "changed, update the hash in cmake/FetchSkia.cmake deliberately.")
  endif()

  message(STATUS "Extracting Skia binaries to ${_dg_skia_lib_dir}")
  file(MAKE_DIRECTORY "${_dg_skia_lib_dir}")
  file(ARCHIVE_EXTRACT INPUT "${_dg_skia_archive}" DESTINATION "${_dg_skia_lib_dir}")

  if(NOT EXISTS "${_dg_skia_library}")
    message(FATAL_ERROR
      "FetchSkia.cmake: ${_dg_skia_asset} extracted but libskia.a is missing at "
      "${_dg_skia_library}. The asset layout may have changed upstream.")
  endif()
endif()

# --- Fetch the headers ------------------------------------------------------

# A blobless, sparse, shallow clone. Skia's full tree is ~95 MB; the headers
# are a small fraction of that, and this project compiles against nothing else.
if(NOT EXISTS "${_dg_skia_include_dir}/include/core/SkCanvas.h")
  find_package(Git REQUIRED)
  message(STATUS "Fetching Skia headers at ${DRAWGUI_SKIA_HEADER_TAG}")

  file(REMOVE_RECURSE "${_dg_skia_include_dir}")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" clone
            --depth 1
            --branch "${DRAWGUI_SKIA_HEADER_TAG}"
            --filter=blob:none
            --sparse
            "${DRAWGUI_SKIA_HEADERS_REPO}"
            "${_dg_skia_include_dir}"
    RESULT_VARIABLE _dg_git_result
    ERROR_VARIABLE _dg_git_error)
  if(NOT _dg_git_result EQUAL 0)
    message(FATAL_ERROR
      "FetchSkia.cmake: failed to clone Skia headers: ${_dg_git_error}")
  endif()

  # Skia's public headers live in include/, and each optional module keeps its
  # own include/ directory. Both are checked out so that enabling a module in
  # a later phase needs no change here.
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" sparse-checkout set include modules
    WORKING_DIRECTORY "${_dg_skia_include_dir}"
    RESULT_VARIABLE _dg_git_result
    ERROR_VARIABLE _dg_git_error)
  if(NOT _dg_git_result EQUAL 0)
    message(FATAL_ERROR
      "FetchSkia.cmake: sparse-checkout failed: ${_dg_git_error}")
  endif()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${_dg_skia_include_dir}"
    OUTPUT_VARIABLE _dg_skia_actual_commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _dg_git_result)
  if(NOT _dg_git_result EQUAL 0)
    message(FATAL_ERROR "FetchSkia.cmake: could not read the fetched header commit")
  endif()

  if(NOT _dg_skia_actual_commit STREQUAL DRAWGUI_SKIA_HEADER_COMMIT)
    file(REMOVE_RECURSE "${_dg_skia_include_dir}")
    message(FATAL_ERROR
      "FetchSkia.cmake: Skia header commit mismatch.\n"
      "  tag:      ${DRAWGUI_SKIA_HEADER_TAG}\n"
      "  expected: ${DRAWGUI_SKIA_HEADER_COMMIT}\n"
      "  actual:   ${_dg_skia_actual_commit}\n"
      "A tag was moved upstream, or the wrong tag is pinned. Headers that do "
      "not match the binary produce a link that succeeds and then misbehaves, "
      "so this is fatal rather than a warning.")
  endif()
endif()

# --- Dependencies of the archive --------------------------------------------

# libskia.a references FreeType unconditionally: SkTypeface_FreeType is linked
# in even when no font API is called, leaving 43 undefined FT_* symbols
# otherwise. Everything else the archive might plausibly want - fontconfig,
# dl, pthread, z, png, jpeg, expat, GL, EGL, X11 - was verified unnecessary by
# removal, re-checked against the ganesh-gl asset. GL in particular stays out:
# this build ships GrGLMakeNativeInterface_none, so every GL entry point is
# resolved at runtime through a caller-supplied proc loader and the archive
# holds no link-time reference to libGL.
find_package(Freetype REQUIRED)

# --- The target -------------------------------------------------------------

add_library(drawgui_skia STATIC IMPORTED GLOBAL)
set_target_properties(drawgui_skia PROPERTIES
  IMPORTED_LOCATION "${_dg_skia_library}")

# Skia headers are included as "include/core/SkCanvas.h", relative to the
# repository root, so the root is the include directory. SYSTEM suppresses
# warnings from Skia's own headers, which matters because this project builds
# with -Werror and Skia emits at least one attribute warning under GCC.
target_include_directories(drawgui_skia SYSTEM INTERFACE "${_dg_skia_include_dir}")
target_link_libraries(drawgui_skia INTERFACE Freetype::Freetype)

# This archive needs zero preprocessor defines, including for the Ganesh API.
# SK_GANESH and SK_GL are build-time switches for compiling Skia itself; in
# the m153 public headers SK_GANESH appears only in SkTypes.h (where it undefs
# the backend macros) and in an Android-framework header, and SK_GL appears
# nowhere at all. GrDirectContext, GrDirectContexts::MakeGL,
# GrBackendRenderTargets::MakeGL and SkSurfaces::WrapBackendRenderTarget are
# therefore declared unconditionally. Measured: preprocessing a translation
# unit that uses all four is byte-identical with and without
# `-DSK_GANESH -DSK_GL`, and so is the resulting object file. Defining them
# would be cargo cult, so they are not defined.

add_library(drawgui::skia ALIAS drawgui_skia)

message(STATUS "Skia: ${_dg_skia_asset_key} (headers ${DRAWGUI_SKIA_HEADER_TAG})")
