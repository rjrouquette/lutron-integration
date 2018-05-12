cmake_minimum_required(VERSION 3.5)
project(lutron_integration)

set(CMAKE_CXX_STANDARD 11)

set(
        SOURCE_FILES
        main.cpp
        lutron_connector.cpp
        lutron_connector.h
        libtelnet.c
        libtelnet.h
        room.cpp
        room.h
        device.cpp
        device.h
        logging.cpp
        logging.h
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
