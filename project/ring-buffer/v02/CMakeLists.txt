add_library(ringbuf_v02 INTERFACE)
target_sources(
    ringbuf_v02
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ringbuf/v02/ring_buffer.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
