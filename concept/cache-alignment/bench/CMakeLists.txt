add_executable(
    cache_align_false_sharing_bench
    cache_align_false_sharing_bench.cpp
)
target_link_libraries(cache_align_false_sharing_bench PRIVATE pthread)
