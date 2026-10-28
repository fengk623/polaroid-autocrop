include(GoogleTest)

function(fk_add_gtest name label)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE fk::core GTest::gtest GTest::gtest_main)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  fk_set_warnings(${name})
  gtest_discover_tests(${name}
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "${label}" SKIP_REGULAR_EXPRESSION "\\[  SKIPPED \\]")
endfunction()

fk_add_gtest(test_version core core/test_version.cpp)
