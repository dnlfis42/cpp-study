add_executable(test_linbuf_v01 test_linear_buffer.cpp)
target_link_libraries(test_linbuf_v01 PRIVATE linbuf_v01 GTest::gtest_main)
add_test(NAME test_linbuf_v01 COMMAND test_linbuf_v01)
