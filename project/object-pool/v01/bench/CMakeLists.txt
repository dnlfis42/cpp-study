add_executable(bench_objpool_v01 bench_object_pool.cpp)
target_link_libraries(
    bench_objpool_v01
    PRIVATE objpool_v01 benchmark::benchmark
)
