add_executable(ringbuf_bench ringbuf_bench.cpp)
target_link_libraries(
    ringbuf_bench
    PRIVATE ringbuf_v01 ringbuf_v02 ringbuf_v03 ringbuf_v04 benchmark::benchmark
)
