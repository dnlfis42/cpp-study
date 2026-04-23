add_executable(objpool_v03_bench objpool_v03_bench.cpp)
target_link_libraries(
    objpool_v03_bench
    PRIVATE objpool_v03 benchmark::benchmark
)
