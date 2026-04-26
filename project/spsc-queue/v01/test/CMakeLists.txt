add_executable(spscq_v01_test spscq_v01_test.cpp)
target_link_libraries(spscq_v01_test PRIVATE spscq_v01 GTest::gtest_main)
add_test(NAME spscq_v01_test COMMAND spscq_v01_test)
