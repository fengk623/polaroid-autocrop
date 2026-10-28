# Dependency superbuild.
#
# Builds every third-party C/C++ library the core needs from pinned git tags into a
# private prefix, as static libraries, so local builds, CI and release builds use the
# same versions on Linux, macOS and Windows (ADR-0001, ADR-0013).
#
# Qt is NOT built here; it is provided by the platform (install-qt-action in CI).
#
#   cmake -S cmake/deps -B build/deps -DFK_DEPS_PREFIX=$PWD/build/deps-install
#   cmake --build build/deps
cmake_minimum_required(VERSION 3.21)
project(fk_deps NONE)
enable_language(C CXX)
include(ExternalProject)

set(FK_DEPS_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install prefix for dependencies")
set(FK_DEPS_BUILD_TYPE "Release" CACHE STRING "Build type for dependencies")
option(FK_DEPS_WITH_TESTS "Also build GoogleTest" ON)

# Single source of truth for dependency versions. Changing a version here is an
# ADR-0013 change: update THIRD-PARTY-LICENSES.md in the same commit.
set(FK_ZLIB_TAG        "v1.3.1")
set(FK_LIBPNG_TAG      "v1.6.59")
set(FK_LIBJPEG_TAG     "3.2.1")
set(FK_LIBTIFF_TAG     "v4.7.2")
set(FK_LCMS2_TAG       "lcms2.19.1")
set(FK_JSON_TAG        "v3.12.0")
set(FK_GTEST_TAG       "v1.17.0")

set(FK_LIBTIFF_REPO "https://github.com/libsdl-org/libtiff.git" CACHE STRING
    "libtiff git repository (GitHub mirror of gitlab.com/libtiff/libtiff)")

set(_common_args
  -DCMAKE_INSTALL_PREFIX=${FK_DEPS_PREFIX}
  -DCMAKE_PREFIX_PATH=${FK_DEPS_PREFIX}
  -DCMAKE_BUILD_TYPE=${FK_DEPS_BUILD_TYPE}
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL
  -DCMAKE_INSTALL_LIBDIR=lib
  -DBUILD_SHARED_LIBS=OFF
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)
# Universal macOS builds pass "arm64;x86_64": ExternalProject needs a different list separator.
set(_png_hw ON)
if(CMAKE_OSX_ARCHITECTURES)
  string(REPLACE ";" "|" _archs "${CMAKE_OSX_ARCHITECTURES}")
  list(APPEND _common_args "-DCMAKE_OSX_ARCHITECTURES=${_archs}")
  list(LENGTH CMAKE_OSX_ARCHITECTURES _narch)
  if(_narch GREATER 1)
    set(_png_hw OFF) # NEON and SSE sources cannot be selected per slice
  endif()
endif()
if(CMAKE_OSX_DEPLOYMENT_TARGET)
  list(APPEND _common_args -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
endif()
if(CMAKE_TOOLCHAIN_FILE)
  list(APPEND _common_args -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif()
if(CMAKE_C_COMPILER)
  list(APPEND _common_args -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
endif()
if(CMAKE_CXX_COMPILER)
  list(APPEND _common_args -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
endif()

ExternalProject_Add(zlib
  LIST_SEPARATOR |
  GIT_REPOSITORY https://github.com/madler/zlib.git
  GIT_TAG ${FK_ZLIB_TAG}
  GIT_SHALLOW ON
  UPDATE_DISCONNECTED ON
  CMAKE_ARGS ${_common_args} -DZLIB_BUILD_EXAMPLES=OFF
)

ExternalProject_Add(libpng
  LIST_SEPARATOR |
  DEPENDS zlib
  GIT_REPOSITORY https://github.com/pnggroup/libpng.git
  GIT_TAG ${FK_LIBPNG_TAG}
  GIT_SHALLOW ON
  UPDATE_DISCONNECTED ON
  CMAKE_ARGS ${_common_args} -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF
             -DPNG_TOOLS=OFF -DPNG_FRAMEWORK=OFF -DPNG_HARDWARE_OPTIMIZATIONS=${_png_hw}
)

ExternalProject_Add(libjpeg
  LIST_SEPARATOR |
  GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
  GIT_TAG ${FK_LIBJPEG_TAG}
  GIT_SHALLOW ON
  UPDATE_DISCONNECTED ON
  CMAKE_ARGS ${_common_args} -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_SIMD=OFF
             -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF
)

ExternalProject_Add(libtiff
  LIST_SEPARATOR |
  DEPENDS zlib libjpeg
  GIT_REPOSITORY ${FK_LIBTIFF_REPO}
  GIT_TAG ${FK_LIBTIFF_TAG}
  GIT_SHALLOW ON
  UPDATE_DISCONNECTED ON
  CMAKE_ARGS ${_common_args} -Dtiff-tools=OFF -Dtiff-tests=OFF -Dtiff-contrib=OFF
             -Dtiff-docs=OFF -Dtiff-deprecated=OFF -Dcxx=OFF -Dzlib=ON -Djpeg=ON
             -Dold-jpeg=OFF -Djbig=OFF -Dlerc=OFF -Dlzma=OFF -Dzstd=OFF -Dwebp=OFF
             -Dlibdeflate=OFF -Dpixarlog=OFF
             -Dchunky-strip-read=ON -Dstrip-chopping=ON
)

ExternalProject_Add(lcms2
  LIST_SEPARATOR |
  GIT_REPOSITORY https://github.com/mm2/Little-CMS.git
  GIT_TAG ${FK_LCMS2_TAG}
  GIT_SHALLOW ON
  UPDATE_DISCONNECTED ON
  # Little-CMS ships autotools/meson/VS projects, not CMake; we provide a minimal
  # CMake build that compiles exactly the library sources.
  PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/lcms2/CMakeLists.txt <SOURCE_DIR>/CMakeLists.txt
  CMAKE_ARGS ${_common_args}
)

ExternalProject_Add(nlohmann_json
  LIST_SEPARATOR |
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG ${FK_JSON_TAG}
  GIT_SHALLOW ON
  UPDATE_DISCONNECTED ON
  CMAKE_ARGS ${_common_args} -DJSON_BuildTests=OFF -DJSON_Install=ON
)

if(FK_DEPS_WITH_TESTS)
  ExternalProject_Add(googletest
    LIST_SEPARATOR |
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG ${FK_GTEST_TAG}
    GIT_SHALLOW ON
    UPDATE_DISCONNECTED ON
    CMAKE_ARGS ${_common_args} -Dgtest_force_shared_crt=ON -DBUILD_GMOCK=OFF
  )
endif()
