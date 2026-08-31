cmake_minimum_required(VERSION 3.20)

project(EdgeLink VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(edgelink_core
    src/protocol.cpp
    src/device_registry.cpp
)
target_include_directories(edgelink_core PUBLIC include)
target_compile_options(edgelink_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(edgelink_gateway src/gateway_main.cpp)
target_link_libraries(edgelink_gateway PRIVATE edgelink_core Threads::Threads)
target_compile_options(edgelink_gateway PRIVATE -Wall -Wextra -Wpedantic)

add_executable(device_simulator src/device_simulator.cpp)
target_link_libraries(device_simulator PRIVATE edgelink_core Threads::Threads)
target_compile_options(device_simulator PRIVATE -Wall -Wextra -Wpedantic)

add_executable(load_generator src/load_generator.cpp)
target_link_libraries(load_generator PRIVATE edgelink_core Threads::Threads)
target_compile_options(load_generator PRIVATE -Wall -Wextra -Wpedantic)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(epoll_gateway src/epoll_gateway_main.cpp)
    target_link_libraries(epoll_gateway PRIVATE edgelink_core Threads::Threads)
    target_compile_options(epoll_gateway PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(protocol_tests tests/protocol_tests.cpp)
target_link_libraries(protocol_tests PRIVATE edgelink_core)
target_compile_options(protocol_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME protocol_tests COMMAND protocol_tests)
