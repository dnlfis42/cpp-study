add_library(objpool_v06 INTERFACE)
target_sources(
    objpool_v06
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/objpool/v06/object_pool.hpp
)
target_link_libraries(objpool_v06 INTERFACE mempool_v03)

add_subdirectory(test)
add_subdirectory(bench)
