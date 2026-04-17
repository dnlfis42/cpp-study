add_executable(bench_objpool_v05 bench_object_pool.cpp)
target_link_libraries(
    bench_objpool_v05
    PRIVATE objpool_v05 benchmark::benchmark
)
