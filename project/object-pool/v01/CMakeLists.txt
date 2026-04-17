add_library(objpool_v01 INTERFACE)
target_sources(
    objpool_v01
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/objpool/v01/object_pool.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
