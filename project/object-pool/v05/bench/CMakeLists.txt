add_executable(objpool_v05_bench objpool_v05_bench.cpp)
target_link_libraries(
    objpool_v05_bench
    PRIVATE objpool_v05 benchmark::benchmark
)
