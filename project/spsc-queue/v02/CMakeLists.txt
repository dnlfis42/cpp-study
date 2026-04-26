add_library(spscq_v02 INTERFACE)
target_sources(
    spscq_v02
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/spscq/v02/spsc_queue.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
