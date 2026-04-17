add_executable(test_linbuf_v02 test_linear_buffer.cpp)
target_link_libraries(test_linbuf_v02 PRIVATE linbuf_v02 GTest::gtest_main)
add_test(NAME test_linbuf_v02 COMMAND test_linbuf_v02)
