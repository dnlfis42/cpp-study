add_library(mempool_v02 INTERFACE)
target_sources(
    mempool_v02
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/mempool/v02/memory_pool.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
