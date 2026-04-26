add_executable(spscq_bench spscq_bench.cpp)
target_link_libraries(
    spscq_bench
    PRIVATE spscq_v02 spscq_v03 spscq_v04 spscq_v05 benchmark::benchmark
)
