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
    stb_image.cpp
    renderobject.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_impl_glfw.cpp
    imgui_impl_opengl3.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    imgui.cpp
)

target_include_directories(test PUBLIC glfw/include glew/include glm cyCodeBase)
target_link_libraries(test arp)