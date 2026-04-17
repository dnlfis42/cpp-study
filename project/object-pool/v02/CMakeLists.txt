add_library(objpool_v02 INTERFACE)
target_sources(
    objpool_v02
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/objpool/v02/object_pool.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
