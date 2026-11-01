add_executable(BookOverridesTest
  BookOverridesTest.cpp
)

target_include_directories(BookOverridesTest PRIVATE
  ${REPO_ROOT}/lib/BookMeta
)

target_link_libraries(BookOverridesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookOverridesTest)
