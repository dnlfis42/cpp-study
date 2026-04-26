add_executable(cache_align_test cache_align_test.cpp)
target_link_libraries(cache_align_test PRIVATE GTest::gtest_main)
add_test(NAME cache_align_test COMMAND cache_align_test)
