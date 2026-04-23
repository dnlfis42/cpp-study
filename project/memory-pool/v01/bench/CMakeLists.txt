add_executable(mempool_v01_bench mempool_v01_bench.cpp)
target_link_libraries(
    mempool_v01_bench
    PRIVATE mempool_v01 benchmark::benchmark
)
