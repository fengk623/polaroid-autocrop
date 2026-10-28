configure_file(include/fk/product_info.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/fk/product_info.hpp @ONLY)

set(FK_CORE_SOURCES
  src/version.cpp
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
