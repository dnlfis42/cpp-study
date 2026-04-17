add_executable(bench_objpool_v04 bench_object_pool.cpp)
target_link_libraries(
    bench_objpool_v04
    PRIVATE objpool_v04 benchmark::benchmark
)
