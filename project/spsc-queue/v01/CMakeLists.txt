add_library(spscq_v01 INTERFACE)
target_sources(
    spscq_v01
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/spscq/v01/spsc_queue.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
