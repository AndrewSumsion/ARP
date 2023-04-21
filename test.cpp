#include <GL/glew.h>
#include "cyGL.h"
#include "cyTriMesh.h"

struct PoseData {
    double rotationX;
    double rotationY;
};
#define ARP_CUSTOM_POSE_DATA
#include "arp.h"
#include "renderobject.h"

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <cmath>

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

static const char* vertSrc =
    "#version 330 core\n"
    "layout(location = 0) in vec3 pos;\n"
    "layout(location = 1) in vec3 normal;\n"
    "uniform mat4 mvp;\n"
    "out vec3 norm;\n"
    "void main() {\n"
    "    gl_Position = mvp * vec4(pos, 1);\n"
    "    norm = normal;\n"
    "}\n"
    ;

static const char* fragSrc =
    "#version 330 core\n"
    "layout(location = 0) out vec4 color;\n"
    "in vec3 norm;\n"
    "void main() {\n"
    "    color = vec4(norm, 1);\n"
    "}\n"
    ;

static arp::Pose poseFunction(
    const arp::Pose& lastPose, double dx, double dy, double dt,
    arp::KeyTimeFunction keyTime);

static void appCallback(GLFWwindow* window);

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
static void windowFocusCallback(GLFWwindow* window, int focused);
static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

static arp::Swapchain* swapchain;
static arp::Swapchain* backgroundSwapchain;
static double aspectRatio = 1;
static double fovY = 90 * M_PI / 180;

static bool shouldReproject = false;
static bool shouldBackground = false;
static bool shouldPredict = false;

int main(int argc, char *argv[]) {
    if (!glfwInit()) {
        std::cout << "Unable to initialize GLFW" << std::endl;
        return -1;
    }

    /* Window hints to get a high enough OpenGL version*/
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    GLFWwindow* window = glfwCreateWindow(640, 480, "ARP Test", NULL, NULL);
    if (!window) {
        std::cout << "Unable to create window" << std::endl;
        return -1;
    }
    glfwMakeContextCurrent(window);

    if(glewInit() != GLEW_OK) {
        std::cout << "Unable to initialize GLEW" << std::endl;
        return -1;
    }

    glfwSetKeyCallback(window, keyCallback);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetWindowFocusCallback(window, windowFocusCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);

    if(arp::initialize() != 0) {
        std::cout << "Unable to initialize arp" << std::endl;
        return -1;
    }

    arp::registerPoseFunction(poseFunction);
    arp::updateProjection(0.1, 100, fovY, aspectRatio);
    arp::startReprojection(appCallback);

    // arp has taken over this thread and blocks until program is over
}

static void appCallback(GLFWwindow* window) {
    swapchain = new arp::Swapchain(640, 480, 3);
    backgroundSwapchain = new arp::Swapchain(swapchain->width / 2, swapchain->height / 2, 3);
    
    std::vector<renderobject> tiles;
    
    double x = -12.4 * 5;
    double y = -12.4 * 3;
    
    for(int i = 0; i < 10; i++)
    {
        for(int j = 0; j < 10; j++)
        {
            tiles.push_back(renderobject("tileFloor1W1.obj", x, -10, y));
            x += 6.2;
        }
        x -= 62;
        y += 12.4;
    }
    
    // renderobject rock1 = renderobject("pileStone4.obj", -12.4, -10, 0);
    renderobject minecart = renderobject("minecartTipW1.obj", -12.4, -10, -12.4);
    
    glEnable(GL_DEPTH_TEST);

    arp::captureCursor();
    
    while(!glfwWindowShouldClose(window)) {
        arp::Pose pose;
        arp::PoseInfo poseInfo;
        if(shouldPredict) {
            double predictedDisplayTime = arp::getPredictedDisplayTime();
            arp::getPredictedCameraPose(predictedDisplayTime, pose, poseInfo);
        }
        else {
            arp::getCameraPose(pose, poseInfo);
        }

        arp::FrameSubmitInfo submitInfo;
        submitInfo.pose = pose;
        submitInfo.poseInfo = poseInfo;

        ///// Main image /////

        int swapchainIndex = swapchain->acquireImage();
        swapchain->bindFramebuffer(swapchainIndex);

        glViewport(0, 0, swapchain->width, swapchain->height);
        glClearColor(0.1, 0.1, 0.1, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        minecart.updateMatrices(pose, aspectRatio, fovY);
        minecart.render();
        
        // minecart.updateMatrices();
        for(int i = 0; i < tiles.size(); i++)
        {
            tiles[i].updateMatrices(pose, aspectRatio, fovY);
            tiles[i].render();
        }
        
        //rock1.updateMatrices(pose, aspectRatio);
        //rock1.render();

        arp::FrameLayer layer;
        layer.flags = arp::NONE;
        layer.fov = fovY;
        layer.swapchain = swapchain;
        layer.swapchainIndex = swapchainIndex;
        if(!shouldReproject)
            layer.flags = arp::CAMERA_LOCKED;
        submitInfo.layers.push_back(layer);

        ///// Background image /////

        if(shouldBackground) {
            double backgroundFovFactor = 1.5;
            int backgroundSwapchainIndex = backgroundSwapchain->acquireImage();
            backgroundSwapchain->bindFramebuffer(backgroundSwapchainIndex);
    
            glViewport(0, 0, backgroundSwapchain->width, backgroundSwapchain->height);
            glClearColor(0.1, 0.1, 0.1, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            
            minecart.updateMatrices(pose, aspectRatio, fovY * backgroundFovFactor);
            minecart.render();
            
            // minecart.updateMatrices();
            for(int i = 0; i < tiles.size(); i++)
            {
                tiles[i].updateMatrices(pose, aspectRatio, fovY * backgroundFovFactor);
                tiles[i].render();
            }
            
            //rock1.updateMatrices(pose, aspectRatio);
            //rock1.render();

            arp::FrameLayer backgroundLayer;
            backgroundLayer.flags = arp::NONE;
            backgroundLayer.fov = fovY * backgroundFovFactor;
            backgroundLayer.swapchain = backgroundSwapchain;
            backgroundLayer.swapchainIndex = backgroundSwapchainIndex;
            if(!shouldReproject)
                backgroundLayer.flags = arp::CAMERA_LOCKED;
            submitInfo.layers.push_back(backgroundLayer);
        }

        arp::submitFrame(submitInfo);
        double fps = 15;
        std::this_thread::sleep_for(std::chrono::milliseconds((int)(1000 / fps)));

        // wait for enter
        //std::string temp;
        //std::getline(std::cin, temp);
    }

    arp::releaseCursor();
}

static double positionSpeed = 10;
static double rotationSpeed = -0.001;

static arp::Pose poseFunction(
    const arp::Pose& lastPose, double dx, double dy, double dt,
    arp::KeyTimeFunction keyTime) {

    arp::Pose result;

    result.data.rotationX = lastPose.data.rotationX + rotationSpeed * dy;
    result.data.rotationY = lastPose.data.rotationY + rotationSpeed * dx;
    result.orientation = glm::quat(glm::vec3(0, result.data.rotationY, 0)) * glm::quat(glm::vec3(result.data.rotationX, 0, 0));

    result.position = lastPose.position;
    
    glm::vec3 movement(0);
    movement.x += positionSpeed * keyTime(GLFW_KEY_D);
    movement.x -= positionSpeed * keyTime(GLFW_KEY_A);
    movement.z += positionSpeed * keyTime(GLFW_KEY_S);
    movement.z -= positionSpeed * keyTime(GLFW_KEY_W);
    
    movement = glm::rotate(glm::mat4(1), (float)result.data.rotationY, glm::vec3(0.f, 1.f, 0.f)) * glm::vec4(movement, 1);
    result.position += movement;

    result.position.y += positionSpeed * keyTime(GLFW_KEY_SPACE);
    result.position.y -= positionSpeed * keyTime(GLFW_KEY_LEFT_SHIFT);

    return result;
}

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if(key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        arp::releaseCursor();
    }
    if(key == GLFW_KEY_1 && action == GLFW_PRESS) {
        shouldReproject = !shouldReproject;
    }
    if(key == GLFW_KEY_2 && action == GLFW_PRESS) {
        shouldBackground = !shouldBackground;
    }
    if(key == GLFW_KEY_3 && action == GLFW_PRESS) {
        shouldPredict = !shouldPredict;
    }
}

static void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    swapchain->resize(width, height);
    backgroundSwapchain->resize(width / 2, height / 2);
}

static void windowFocusCallback(GLFWwindow* window, int focused) {
    if(focused) {
        arp::captureCursor();
    }
}

static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if(action == GLFW_PRESS) {
        arp::captureCursor();
    }
}
