add_executable(spscq_v04_test spscq_v04_test.cpp)
target_link_libraries(spscq_v04_test PRIVATE spscq_v04 GTest::gtest_main)
add_test(NAME spscq_v04_test COMMAND spscq_v04_test)
