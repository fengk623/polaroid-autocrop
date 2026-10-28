include(GoogleTest)

add_library(fk_test_support STATIC support/test_support.cpp)
target_link_libraries(fk_test_support PUBLIC fk::core TIFF::tiff GTest::gtest)
target_include_directories(fk_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
fk_set_warnings(fk_test_support)

function(fk_add_gtest name label)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE fk::core fk_test_support TIFF::tiff GTest::gtest GTest::gtest_main)
  fk_set_warnings(${name})
  gtest_discover_tests(${name}
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "${label}" SKIP_REGULAR_EXPRESSION "\\[  SKIPPED \\]")
endfunction()

fk_add_gtest(test_version core core/test_version.cpp)
fk_add_gtest(test_geometry geometry core/test_geometry.cpp)
fk_add_gtest(test_tiff_io io core/test_tiff_io.cpp)
fk_add_gtest(test_png_jpeg io core/test_png_jpeg.cpp)
fk_add_gtest(test_color color core/test_color.cpp)
fk_add_gtest(test_metadata metadata core/test_metadata.cpp)
fk_add_gtest(test_memory core core/test_memory.cpp)
fk_add_gtest(test_export export core/test_export.cpp)
fk_add_gtest(test_detection detection core/test_detection.cpp)
target_include_directories(test_detection PRIVATE ${PROJECT_SOURCE_DIR}/core/src)
fk_add_gtest(test_composite composite core/test_composite.cpp)
fk_add_gtest(test_relink relink core/test_relink.cpp)
fk_add_gtest(test_project project core/test_project.cpp)

# Detection evaluation on a generated corpus (TEST-PLAN §4, RFC-0016). Needs the tools.
if(FK_BUILD_TOOLS)
  add_test(NAME eval_corpus
    COMMAND ${CMAKE_COMMAND}
      -DFKFIXTURE=$<TARGET_FILE:fkfixture> -DFKCLI=$<TARGET_FILE:fkcli>
      -DCORPUS_DIR=${CMAKE_BINARY_DIR}/eval-corpus -DCOUNT=12 -DSEED=1
      -DREPORT=${CMAKE_BINARY_DIR}/eval-report.json
      -P ${CMAKE_CURRENT_SOURCE_DIR}/eval_corpus.cmake)
  set_tests_properties(eval_corpus PROPERTIES LABELS eval TIMEOUT 900)
endif()
