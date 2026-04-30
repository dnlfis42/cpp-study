add_executable(shandle_bench shandle_bench.cpp)
target_link_libraries(
    shandle_bench
    PRIVATE shandle_v01 shandle_v02 benchmark::benchmark
)
