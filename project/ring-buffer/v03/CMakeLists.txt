add_library(ringbuf_v03 INTERFACE)
target_sources(
    ringbuf_v03
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ringbuf/v03/ring_buffer.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
