add_library(linbuf_v02 INTERFACE)
target_sources(
    linbuf_v02
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/linbuf/v02/linear_buffer.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
