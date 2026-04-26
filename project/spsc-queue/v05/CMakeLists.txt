add_library(spscq_v05 INTERFACE)
target_sources(
    spscq_v05
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/spscq/v05/spsc_queue.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
