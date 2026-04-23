add_executable(bench_mempool_v03 bench_memory_pool.cpp)
target_link_libraries(
    bench_mempool_v03
    PRIVATE mempool_v03 benchmark::benchmark
)
