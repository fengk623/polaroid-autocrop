# fkfixture — deterministic synthetic flatbed scans + ground truth (RFC-0016).
add_executable(fkfixture
  main.cpp
  scene.cpp
  render.cpp
  output.cpp
)
target_link_libraries(fkfixture PRIVATE TIFF::tiff PNG::PNG ZLIB::ZLIB lcms2::lcms2
  nlohmann_json::nlohmann_json Threads::Threads)
target_compile_features(fkfixture PRIVATE cxx_std_20)
fk_set_warnings(fkfixture)
# Bit-identical output on every platform: no fused multiply-add contraction (Apple Clang on arm64
# contracts by default), strict IEEE semantics on MSVC.
if(MSVC)
  target_compile_options(fkfixture PRIVATE /fp:precise)
else()
  target_compile_options(fkfixture PRIVATE -ffp-contract=off)
endif()
