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
#include <chrono>
#include <thread>
#define _USE_MATH_DEFINES
#include <cmath>

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

static const char* meshPath;
static arp::Swapchain* swapchain;
static double aspectRatio = 1;

int main(int argc, char *argv[]) {
    if(argc != 2) {
        std::cout << "wrong number of arguments" << std::endl;
        return 1;
    }
    meshPath = argv[1];

    if (!glfwInit()) {
        std::cout << "Unable to initialize GLFW" << std::endl;
        return -1;
    }

    /* Window hints to get a high enough OpenGL version*/
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
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

    glfwSetKeyCallback(window, keyCallback);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetWindowFocusCallback(window, windowFocusCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);

    if(arp::initialize() != 0) {
        std::cout << "Unable to initialize arp" << std::endl;
        return -1;
    }

    arp::registerPoseFunction(poseFunction);
    arp::updateProjection(0.1, 100, 90 * 3.14159265358979324 / 180, aspectRatio);
    arp::startReprojection(appCallback);

    // arp has taken over this thread and blocks until program is over
}

static void appCallback(GLFWwindow* window) {
    swapchain = new arp::Swapchain(640, 480, 3);
    
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
        int swapchainIndex = swapchain->acquireImage();
        swapchain->bindFramebuffer(swapchainIndex);
        
        
        arp::Pose pose;
        arp::PoseInfo poseInfo;
        arp::getCameraPose(pose, poseInfo);

        glViewport(0, 0, swapchain->width, swapchain->height);
        glClearColor(0.1, 0.1, 0.1, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 projection = glm::perspective(0.5f * 3.14159265358979324f, (float)aspectRatio, 0.1f, 100.f);
        glm::mat4 camera = glm::translate(glm::mat4(1), pose.position) * glm::mat4(pose.orientation);
        glm::mat4 view = glm::inverse(camera);
        glm::mat4 mvp = projection * view;
        
        minecart.updateMatrices(pose, aspectRatio);
        minecart.render();
        
       // minecart.updateMatrices();
        for(int i = 0; i < tiles.size(); i++)
        {
            tiles[i].updateMatrices(pose, aspectRatio);
            tiles[i].render();
        }
        
        //rock1.updateMatrices(pose, aspectRatio);
        //rock1.render();
        
        
        

        arp::FrameSubmitInfo submitInfo;
        submitInfo.pose = pose;
        submitInfo.poseInfo = poseInfo;

        arp::FrameLayer layer;
        layer.flags = arp::FrameLayerFlags::NONE;
        layer.fov = 3.14159265358979324 / 2;
        layer.swapchain = swapchain;
        layer.swapchainIndex = swapchainIndex;

        submitInfo.layers.push_back(layer);

        arp::submitFrame(submitInfo);
        double fps = 1;
        std::this_thread::sleep_for(std::chrono::milliseconds((int)(1000 / fps)));

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
}

static void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    swapchain->resize(width, height);
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
