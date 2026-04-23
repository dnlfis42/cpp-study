add_executable(linbuf_v02_test linbuf_v02_test.cpp)
target_link_libraries(linbuf_v02_test PRIVATE linbuf_v02 GTest::gtest_main)
add_test(NAME linbuf_v02_test COMMAND linbuf_v02_test)
