find_package(GTest REQUIRED)

add_executable(ll_tests
  text_normalize_test.cpp
  lecture_splitter_test.cpp
  chunker_test.cpp
  ocr_core_test.cpp
  search_core_test.cpp
  mcp_test.cpp
  agent_test.cpp
)
target_link_libraries(ll_tests PRIVATE ll_core ll_mcp GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(ll_tests)
