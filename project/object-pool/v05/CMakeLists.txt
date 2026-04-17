add_library(objpool_v05 INTERFACE)
target_sources(
    objpool_v05
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/objpool/v05/object_pool.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
