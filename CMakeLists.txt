cmake_minimum_required(VERSION 3.25)
project(cpp-study CXX)

# 기본 빌드 타입
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# C++ 표준
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# IDE/clangd 연동
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 컬러 진단
set(CMAKE_COLOR_DIAGNOSTICS ON)

# 컴파일 옵션
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Werror
)

# ccache (있으면 사용)
find_program(CCACHE ccache)
if(CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
endif()

# 출력 경로
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 의존성
find_package(GTest REQUIRED)
find_package(benchmark REQUIRED)

# 테스트 활성화
enable_testing()

# 프로젝트 경로
# add_subdirectory(concept)
# add_subdirectory(sandbox)
add_subdirectory(project)
