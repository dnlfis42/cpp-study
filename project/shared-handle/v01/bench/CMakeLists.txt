add_executable(shandle_v01_bench shandle_v01_bench.cpp)
target_link_libraries(
    shandle_v01_bench
    PRIVATE shandle_v01 benchmark::benchmark
)
