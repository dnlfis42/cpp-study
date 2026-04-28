add_library(hazard_pointer INTERFACE)
target_sources(
    hazard_pointer
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/hazard_pointer.hpp include/lf_stack.hpp
)
add_subdirectory(test)
