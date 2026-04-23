add_executable(bench_mempool_v01 bench_memory_pool.cpp)
target_link_libraries(
    bench_mempool_v01
    PRIVATE mempool_v01 benchmark::benchmark
)
