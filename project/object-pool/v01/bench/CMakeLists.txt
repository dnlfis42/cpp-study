add_executable(objpool_v01_bench objpool_v01_bench.cpp)
target_link_libraries(
    objpool_v01_bench
    PRIVATE objpool_v01 benchmark::benchmark
)
