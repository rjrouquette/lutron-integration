cmake_minimum_required(VERSION 3.8)
project(lutron_integration)

set(CMAKE_CXX_STANDARD 11)

set(
        SOURCE_FILES
        main.cpp
)

add_executable(
        lutron-integration
        ${SOURCE_FILES}
)

target_link_libraries(
        lutron-integration
        -lssh
        -ljson-c
        -pthread
)
