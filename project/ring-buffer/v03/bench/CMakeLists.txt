add_executable(ringbuf_v03_bench ringbuf_v03_bench.cpp)
target_link_libraries(
    ringbuf_v03_bench
    PRIVATE ringbuf_v03 benchmark::benchmark
)
