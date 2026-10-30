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
    PROPERTIES LABELS "${label}" SKIP_REGULAR_EXPRESSION "  SKIPPED ")
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

# Known issues (Stage A): each test fails on the current code and is registered with WILL_FAIL, so a fix makes it
# fail loudly and the fixing unit moves it into the regular suite (docs/v1/TRACEABILITY.md).
add_executable(test_known_issues core/test_known_issues.cpp)
target_link_libraries(test_known_issues PRIVATE fk::core fk_test_support GTest::gtest GTest::gtest_main)
fk_set_warnings(test_known_issues)
foreach(_ki FK004_RemoveIdentityDropsCreatorFromSourceXmp FK007_ReplacedScanAtSamePathIsNotTrusted
            FK008_DamagedProjectOffersItsBackup)
  add_test(NAME KnownIssue.${_ki} COMMAND test_known_issues --gtest_filter=KnownIssue.${_ki})
  set_tests_properties(KnownIssue.${_ki} PROPERTIES WILL_FAIL TRUE LABELS known-issue)
endforeach()

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
