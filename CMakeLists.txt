cmake_minimum_required(VERSION 3.15)
project(arp)

add_subdirectory(glfw)
add_subdirectory(glew/build/cmake)

add_executable(
    arp
    arp.cpp
)

target_include_directories(arp PUBLIC glfw/include glew/include)
target_link_libraries(arp glfw glew_s)