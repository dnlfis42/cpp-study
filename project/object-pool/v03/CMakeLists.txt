add_library(objpool_v03 INTERFACE)
target_sources(
    objpool_v03
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/objpool/v03/object_pool.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
