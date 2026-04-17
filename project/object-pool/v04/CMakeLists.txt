add_library(objpool_v04 INTERFACE)
target_sources(
    objpool_v04
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/objpool/v04/object_pool.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
