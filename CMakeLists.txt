cmake_minimum_required(VERSION 3.20)
project(ArchNet VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ARCHNET_BUILD_TESTS "Build ArchNet's test suite" ON)

add_library(arch_network STATIC
    src/url.cpp
    src/http_message.cpp
)
target_include_directories(arch_network PUBLIC include)

if(ARCHNET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
