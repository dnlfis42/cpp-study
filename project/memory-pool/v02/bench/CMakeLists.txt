add_executable(mempool_v02_bench mempool_v02_bench.cpp)
target_link_libraries(
    mempool_v02_bench
    PRIVATE mempool_v02 benchmark::benchmark
)
