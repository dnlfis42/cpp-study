add_executable(bench_ringbuf_v03 bench_ring_buffer.cpp)
target_link_libraries(
    bench_ringbuf_v03
    PRIVATE ringbuf_v03 benchmark::benchmark
)
