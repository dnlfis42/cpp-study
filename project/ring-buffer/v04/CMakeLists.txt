add_library(ringbuf_v04 INTERFACE)
target_sources(
    ringbuf_v04
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ringbuf/v04/ring_buffer.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
