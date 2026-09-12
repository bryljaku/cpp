cmake_minimum_required(VERSION 3.10.0)
project(program VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.1.0
)

FetchContent_MakeAvailable(fmt)

add_executable(program main.cpp shared_pointer.h)
target_link_libraries(program PRIVATE fmt::fmt)
