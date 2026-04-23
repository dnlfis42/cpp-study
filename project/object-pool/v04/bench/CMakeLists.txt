add_executable(objpool_v04_bench objpool_v04_bench.cpp)
target_link_libraries(
    objpool_v04_bench
    PRIVATE objpool_v04 benchmark::benchmark
)
