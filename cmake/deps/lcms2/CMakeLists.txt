# Minimal CMake build for Little-CMS 2 (library only), used by the dependency superbuild.
cmake_minimum_required(VERSION 3.16)
project(lcms2 C)
file(GLOB LCMS2_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_library(lcms2 STATIC ${LCMS2_SOURCES})
target_include_directories(lcms2 PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_include_directories(lcms2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(lcms2 PUBLIC CMS_NO_REGISTER_KEYWORD=1)
if(NOT MSVC)
  target_link_libraries(lcms2 PUBLIC m)
endif()
install(TARGETS lcms2 EXPORT lcms2Targets ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(FILES include/lcms2.h include/lcms2_plugin.h DESTINATION include)
install(EXPORT lcms2Targets NAMESPACE lcms2:: DESTINATION lib/cmake/lcms2 FILE lcms2Config.cmake)
