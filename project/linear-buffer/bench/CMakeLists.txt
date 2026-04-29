add_executable(linbuf_bench linbuf_bench.cpp)
target_link_libraries(
    linbuf_bench
    PRIVATE linbuf_v01 linbuf_v02 benchmark::benchmark
)
