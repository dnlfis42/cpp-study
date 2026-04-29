add_executable(objpool_bench objpool_bench.cpp)
target_link_libraries(
    objpool_bench
    PRIVATE
        objpool_v01
        objpool_v02
        objpool_v03
        objpool_v04
        objpool_v05
        benchmark::benchmark
)
