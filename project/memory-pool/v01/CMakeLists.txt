add_library(mempool_v01 INTERFACE)
target_sources(
    mempool_v01
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/mempool/v01/memory_pool.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
