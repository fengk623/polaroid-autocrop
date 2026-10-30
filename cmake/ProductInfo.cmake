# Single source of truth for product identity and version (ADR-0012).
# Code reads these through the generated header fk/product_info.hpp.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" FK_VERSION LIMIT_COUNT 1)
# re-run CMake when VERSION changes, so an existing build directory never keeps a stale version
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../VERSION")
string(STRIP "${FK_VERSION}" FK_VERSION)
# VERSION may carry a SemVer pre-release suffix (0.6.0-alpha.1). CMake, Info.plist and the Windows VERSIONINFO need
# the numeric part; everything shown to people uses the full string.
if(NOT FK_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.-]+)?$")
  message(FATAL_ERROR "VERSION must be MAJOR.MINOR.PATCH[-prerelease], got '${FK_VERSION}'")
endif()
set(FK_VERSION_NUMERIC "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
set(FK_VERSION_PRERELEASE "${CMAKE_MATCH_4}")

# Build provenance (FK-014): the commit and build that produced this binary. CI passes FK_BUILD_ID
# (run number.attempt); FK_GIT_SHA defaults to `git rev-parse HEAD` at configure time.
if(NOT DEFINED FK_GIT_SHA OR FK_GIT_SHA STREQUAL "")
  find_package(Git QUIET)
  set(FK_GIT_SHA "unknown")
  if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
                    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
                    OUTPUT_VARIABLE _fk_sha OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _fk_rc)
    if(_fk_rc EQUAL 0 AND _fk_sha MATCHES "^[0-9a-f]+$")
      set(FK_GIT_SHA "${_fk_sha}")
      execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
                      WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
                      OUTPUT_VARIABLE _fk_dirty OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      if(NOT _fk_dirty STREQUAL "")
        set(FK_GIT_SHA "${FK_GIT_SHA}-dirty")
      endif()
    endif()
  endif()
endif()
if(NOT DEFINED FK_BUILD_ID OR FK_BUILD_ID STREQUAL "")
  if(DEFINED ENV{FK_BUILD_ID} AND NOT "$ENV{FK_BUILD_ID}" STREQUAL "")
    set(FK_BUILD_ID "$ENV{FK_BUILD_ID}")
  else()
    set(FK_BUILD_ID "local")
  endif()
endif()
set(FK_PRODUCT_NAME "FrameKeeper")
set(FK_PRODUCT_ID "framekeeper")
set(FK_EXECUTABLE_NAME "FrameKeeper")
set(FK_CLI_NAME "fkcli")
set(FK_BUNDLE_ID "io.github.fengk623.framekeeper")
set(FK_ORGANIZATION "FrameKeeper")
set(FK_PROJECT_EXT "fkproj")
set(FK_SIDECAR_SUFFIX ".fk.json")
set(FK_TEMPLATE_SUFFIX ".fktemplate.json")
set(FK_XMP_NAMESPACE "https://github.com/fengk623/polaroid-autocrop/ns/1.0/")
set(FK_REPO_URL "https://github.com/fengk623/polaroid-autocrop")
set(FK_MACOS_MIN "12.0")
