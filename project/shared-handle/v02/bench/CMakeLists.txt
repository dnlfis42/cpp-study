add_executable(shandle_v02_bench shandle_v02_bench.cpp)
target_link_libraries(
    shandle_v02_bench
    PRIVATE shandle_v02 benchmark::benchmark
)
