add_library(shandle_v01 INTERFACE)
target_sources(
    shandle_v01
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/shandle/v01/shared_handle.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
