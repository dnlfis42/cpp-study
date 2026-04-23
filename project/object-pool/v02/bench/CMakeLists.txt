add_executable(objpool_v02_bench objpool_v02_bench.cpp)
target_link_libraries(
    objpool_v02_bench
    PRIVATE objpool_v02 benchmark::benchmark
)
