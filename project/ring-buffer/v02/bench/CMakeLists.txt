add_executable(bench_ringbuf_v02 bench_ring_buffer.cpp)
target_link_libraries(
    bench_ringbuf_v02
    PRIVATE ringbuf_v02 benchmark::benchmark
)
