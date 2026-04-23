add_library(mempool_v03 INTERFACE)
target_sources(
    mempool_v03
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/mempool/v03/memory_pool.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
