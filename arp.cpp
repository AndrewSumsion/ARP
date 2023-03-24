#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <string>

int main() {
    if(!glfwInit()) {
        std::cout << "Unable to initialize glfw" << std::endl;
        return -1;
    }

    GLFWwindow* window = glfwCreateWindow(640, 480, "Test Window", nullptr, nullptr);
    if(!window) {
        std::cout << "Unable to create window" << std::endl;
        return -1;
    }

    glfwMakeContextCurrent(window);

    GLenum glewErr = glewInit();
    if(glewErr != GLEW_OK) {
        std::cout << "Unable to initialize GLEW: " << glewErr << std::endl;
        return -1;
    }

    std::string in;
    std::getline(std::cin, in);

    return 0;
}