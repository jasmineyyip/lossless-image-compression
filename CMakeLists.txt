cmake_minimum_required(VERSION 3.15)
project(image_compression CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(entropy_gap entropy_gap.cpp)
target_include_directories(entropy_gap PRIVATE deps)
target_compile_options(entropy_gap PRIVATE -Wall -Wextra -Wpedantic)

add_executable(compress compress.cpp)
target_include_directories(compress PRIVATE deps)
target_compile_options(compress PRIVATE -Wall -Wextra -Wpedantic)

add_executable(decompress decompress.cpp)
target_include_directories(decompress PRIVATE deps)
target_compile_options(decompress PRIVATE -Wall -Wextra -Wpedantic)