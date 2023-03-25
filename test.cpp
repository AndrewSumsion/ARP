#include "arp.h"
#include <GL/glew.h>

#include <iostream>
#include <chrono>
#include <thread>

static arp::Pose poseFunction(
    const arp::Pose& original, double dx, double dy,
    double dt,
    const std::unordered_map<int, double>& keys);

static void appCallback(GLFWwindow* window);

int main() {
    if (!glfwInit()) {
        std::cout << "Unable to initialize GLFW" << std::endl;
        return -1;
    }

    GLFWwindow* window = glfwCreateWindow(640, 480, "My Title", NULL, NULL);
    if (!window) {
        std::cout << "Unable to create window" << std::endl;
        return -1;
    }
    glfwMakeContextCurrent(window);

    if(glewInit() != GLEW_OK) {
        std::cout << "Unable to initialize GLEW" << std::endl;
        return -1;
    }

    if(arp::initialize() != 0) {
        std::cout << "Unable to initialize arp" << std::endl;
        return -1;
    }

    arp::registerPoseFunction(poseFunction);
    arp::updateProjection(0.1, 100, 90, 1);
    arp::startReprojection(appCallback);

    // arp has taken over this thread, at this point the program is over
}

static void appCallback(GLFWwindow* window) {
    arp::Swapchain swapchain(640, 480, 3);

    arp::captureCursor();

    while(!glfwWindowShouldClose(window)) {
        int swapchainIndex = swapchain.acquireImage();
        GLuint texture = swapchain.images[swapchainIndex];
        arp::Pose pose = arp::getNextPose();

        // don't actually render anything

        arp::FrameSubmitInfo submitInfo;
        submitInfo.pose = pose;

        arp::FrameLayer layer;
        layer.flags = arp::FrameLayerFlags::NONE;
        layer.fov = 3.14159265358979324 / 2;
        layer.swapchain = &swapchain;
        layer.swapchainIndex = swapchainIndex;

        submitInfo.layers.push_back(layer);

        arp::submitFrame(submitInfo);
        int fps = 2;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / fps));

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    arp::releaseCursor();
}

static double positionSpeed = 1;
static double rotationSpeed = 0.001;

static arp::Pose poseFunction(
    const arp::Pose& original, double dx, double dy,
    double dt,
    const std::unordered_map<int, double>& keys) {

    arp::Pose result;
    glm::vec3 euler = glm::vec3(dy * rotationSpeed, dx * rotationSpeed, 0);
    result.orientation = glm::quat(euler) * original.orientation;

    result.position = original.position;
    
    if(keys.find(GLFW_KEY_D) != keys.end()) {
        result.position.x += positionSpeed * keys.at(GLFW_KEY_D);
    }
    if(keys.find(GLFW_KEY_A) != keys.end()) {
        result.position.x -= positionSpeed * keys.at(GLFW_KEY_A);
    }
    if(keys.find(GLFW_KEY_SPACE) != keys.end()) {
        result.position.y += positionSpeed * keys.at(GLFW_KEY_SPACE);
    }
    if(keys.find(GLFW_KEY_LEFT_SHIFT) != keys.end()) {
        result.position.y -= positionSpeed * keys.at(GLFW_KEY_LEFT_SHIFT);
    }
    if(keys.find(GLFW_KEY_W) != keys.end()) {
        result.position.z += positionSpeed * keys.at(GLFW_KEY_W);
    }
    if(keys.find(GLFW_KEY_S) != keys.end()) {
        result.position.z -= positionSpeed * keys.at(GLFW_KEY_S);
    }

    return result;
}