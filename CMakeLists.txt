cmake_minimum_required(VERSION 3.15)
project(arp)

add_subdirectory(glfw)
add_subdirectory(glew/build/cmake)

add_library(
    arp STATIC
    arp.cpp
)

target_include_directories(arp PUBLIC glfw/include glew/include glm)
target_link_libraries(arp glfw glew_s)

add_executable(
    test
    test.cpp
)

target_include_directories(test PUBLIC glfw/include glew/include glm cyCodeBase)
target_link_libraries(test arp)