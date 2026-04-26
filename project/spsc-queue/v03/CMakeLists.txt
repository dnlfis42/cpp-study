add_library(spscq_v03 INTERFACE)
target_sources(
    spscq_v03
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/spscq/v03/spsc_queue.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
