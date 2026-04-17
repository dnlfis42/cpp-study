add_executable(bench_ringbuf_v01 bench_ring_buffer.cpp)
target_link_libraries(
    bench_ringbuf_v01
    PRIVATE ringbuf_v01 benchmark::benchmark
)
