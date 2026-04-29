add_library(objpool_v05 INTERFACE)
target_sources(
    objpool_v05
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/objpool/v05/object_pool.hpp
)
target_link_libraries(objpool_v05 INTERFACE mempool_v03)

add_subdirectory(test)
add_subdirectory(bench)
