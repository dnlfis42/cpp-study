add_executable(spscq_v03_test spscq_v03_test.cpp)
target_link_libraries(spscq_v03_test PRIVATE spscq_v03 GTest::gtest_main)
add_test(NAME spscq_v03_test COMMAND spscq_v03_test)
