add_executable(spscq_v05_test spscq_v05_test.cpp)
target_link_libraries(spscq_v05_test PRIVATE spscq_v05 GTest::gtest_main)
add_test(NAME spscq_v05_test COMMAND spscq_v05_test)
