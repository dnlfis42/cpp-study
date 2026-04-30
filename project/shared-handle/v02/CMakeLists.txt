add_library(shandle_v02 INTERFACE)
target_sources(
    shandle_v02
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/shandle/v02/shared_handle.hpp
)

add_subdirectory(test)
add_subdirectory(bench)
