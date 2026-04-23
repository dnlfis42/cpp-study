add_executable(ringbuf_v02_bench ringbuf_v02_bench.cpp)
target_link_libraries(
    ringbuf_v02_bench
    PRIVATE ringbuf_v02 benchmark::benchmark
)
