add_executable(bench_ringbuf_v04 bench_ring_buffer.cpp)
target_link_libraries(
    bench_ringbuf_v04
    PRIVATE ringbuf_v04 benchmark::benchmark
)
