add_executable(spscq_v02_test spscq_v02_test.cpp)
target_link_libraries(spscq_v02_test PRIVATE spscq_v02 GTest::gtest_main)
add_test(NAME spscq_v02_test COMMAND spscq_v02_test)
