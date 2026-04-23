add_executable(mempool_v03_bench mempool_v03_bench.cpp)
target_link_libraries(
    mempool_v03_bench
    PRIVATE mempool_v03 benchmark::benchmark
)
