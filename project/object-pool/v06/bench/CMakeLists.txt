add_executable(objpool_v06_bench objpool_v06_bench.cpp)
target_link_libraries(
    objpool_v06_bench
    PRIVATE objpool_v05 objpool_v06 benchmark::benchmark
)
