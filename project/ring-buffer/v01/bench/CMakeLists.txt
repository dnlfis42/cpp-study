add_executable(ringbuf_v01_bench ringbuf_v01_bench.cpp)
target_link_libraries(
    ringbuf_v01_bench
    PRIVATE ringbuf_v01 benchmark::benchmark
)
