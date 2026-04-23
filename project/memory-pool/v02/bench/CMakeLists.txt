add_executable(bench_mempool_v02 bench_memory_pool.cpp)
target_link_libraries(
    bench_mempool_v02
    PRIVATE mempool_v02 benchmark::benchmark
)
