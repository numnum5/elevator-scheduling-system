cmake_minimum_required(VERSION 3.20)
project(elevator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(logger
    src/logger.cpp
    include/logger.h
)

target_include_directories(logger
    PUBLIC include
)

add_library(tcp
    src/tcp.cpp
    include/tcp.h
)

target_include_directories(tcp
    PUBLIC include
)

add_executable(elevator
    src/main.cpp
)

add_executable(call
    src/call.cpp
)

add_executable(car
    src/car.cpp
)

add_executable(internal
    src/internal.cpp
)

add_executable(controller
    src/server.cpp
)

set(ELEVATOR_EXECUTABLES
    elevator
    call
    car
    internal
    controller
)

foreach(target IN LISTS ELEVATOR_EXECUTABLES)
    target_link_libraries(${target} PRIVATE tcp logger)
endforeach()

add_custom_target(build_all
    DEPENDS
        logger
        tcp
        ${ELEVATOR_EXECUTABLES}
)
