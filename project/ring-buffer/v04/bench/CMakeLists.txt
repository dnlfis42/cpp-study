add_executable(ringbuf_v04_bench ringbuf_v04_bench.cpp)
target_link_libraries(
    ringbuf_v04_bench
    PRIVATE ringbuf_v04 benchmark::benchmark
)
