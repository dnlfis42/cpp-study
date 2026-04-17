add_executable(bench_objpool_v03 bench_object_pool.cpp)
target_link_libraries(
    bench_objpool_v03
    PRIVATE objpool_v03 benchmark::benchmark
)
