add_library(spscq_v04 INTERFACE)
target_sources(
    spscq_v04
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/spscq/v04/spsc_queue.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
