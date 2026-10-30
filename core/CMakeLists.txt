configure_file(include/fk/product_info.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/fk/product_info.hpp @ONLY)
# Embed the film-type library (single source: core/detection/film_types.json).
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/detection/film_types.json FK_FILM_TYPES_JSON)
configure_file(src/detection/film_types_json.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/film_types_json.hpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/detection/film_types.json)

set(FK_CORE_SOURCES
  src/version.cpp
  src/common.cpp
  src/util.cpp
  src/memory.cpp
  src/image.cpp
  src/sha256.cpp
  src/color.cpp
  src/metadata.cpp
  src/geometry.cpp
  src/tiff_util.cpp
  src/tiff_source.cpp
  src/tiff_sink.cpp
  src/image_file.cpp
  src/detection/cv.cpp
  src/detection/proxy.cpp
  src/detection/film_types.cpp
  src/detection/detect.cpp
  src/detection/refine.cpp
  src/pixel_ops.cpp
  src/relink.cpp
  src/relink_assess.cpp
  src/output_stage.cpp
  src/export.cpp
  src/composite.cpp
  src/project.cpp
)

add_library(fkcore STATIC ${FK_CORE_SOURCES})
add_library(fk::core ALIAS fkcore)
target_include_directories(fkcore PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_include_directories(fkcore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(fkcore PUBLIC Threads::Threads PRIVATE TIFF::tiff PNG::PNG JPEG::JPEG ZLIB::ZLIB
  lcms2::lcms2 nlohmann_json::nlohmann_json)
fk_set_warnings(fkcore)
