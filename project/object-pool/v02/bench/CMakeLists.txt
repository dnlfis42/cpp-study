add_executable(bench_objpool_v02 bench_object_pool.cpp)
target_link_libraries(
    bench_objpool_v02
    PRIVATE objpool_v02 benchmark::benchmark
)
