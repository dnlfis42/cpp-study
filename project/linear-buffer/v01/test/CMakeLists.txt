add_executable(linbuf_v01_test linbuf_v01_test.cpp)
target_link_libraries(linbuf_v01_test PRIVATE linbuf_v01 GTest::gtest_main)
add_test(NAME linbuf_v01_test COMMAND linbuf_v01_test)
