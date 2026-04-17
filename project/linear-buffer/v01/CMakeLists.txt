add_library(linbuf_v01 INTERFACE)
target_sources(
    linbuf_v01
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/linbuf/v01/linear_buffer.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
