#include <GL/glew.h>
#include <cyGL.h>
#include <cyTriMesh.h>

struct PoseData {
    double rotationX;
    double rotationY;
};
#define ARP_CUSTOM_POSE_DATA
#include "arp.h"

#include <iostream>
#include <chrono>
#include <thread>
#define _USE_MATH_DEFINES
#include <cmath>

static const char* vertSrc =
    "#version 330 core\n"
    "layout(location = 0) in vec3 pos;\n"
    "uniform mat4 mvp;\n"
    "void main() {\n"
    "    gl_Position = mvp * vec4(pos, 1);\n"
    "}\n"
    ;

static const char* fragSrc =
    "#version 330 core\n"
    "layout(location = 0) out vec4 color;\n"
    "void main() {\n"
    "    color = vec4(1, 0, 0, 1);\n"
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

    cy::TriMesh mesh;
    mesh.LoadFromFileObj(meshPath, false);

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    std::vector<cy::Vec3f> vboData;
    //vboData.push_back(cy::Vec3f(-1, 1, 0));
    //vboData.push_back(cy::Vec3f(-1, -1, 0));
    //vboData.push_back(cy::Vec3f(1, -1, 0));
    for(int i = 0; i < mesh.NF(); i++) {
        cy::TriMesh::TriFace face = mesh.F(i);
        vboData.push_back(mesh.V(face.v[0]));
        vboData.push_back(mesh.V(face.v[1]));
        vboData.push_back(mesh.V(face.v[2]));
    }

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cy::Vec3f) * vboData.size(), vboData.data(), GL_STATIC_DRAW);


    cy::GLSLProgram program;
    program.BuildSources(vertSrc, fragSrc);

    GLint posLoc = program.AttribLocation("pos");
    glEnableVertexAttribArray(posLoc);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);

    arp::captureCursor();

    while(!glfwWindowShouldClose(window)) {
        int swapchainIndex = swapchain->acquireImage();
        swapchain->bindFramebuffer(swapchainIndex);

        arp::Pose pose;
        arp::PoseInfo poseInfo;
        arp::getCameraPose(pose, poseInfo);

        glViewport(0, 0, swapchain->width, swapchain->height);
        glClearColor(0, 0.5, 0.5, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 projection = glm::perspective(0.5f * 3.14159265358979324f, (float)aspectRatio, 0.1f, 100.f);
        glm::mat4 camera = glm::translate(glm::mat4(1), pose.position) * glm::mat4(pose.orientation);
        glm::mat4 view = glm::inverse(camera);
        glm::mat4 mvp = projection * view;

        program.SetUniformMatrix4("mvp", &mvp[0][0]);

        program.Bind();
        glDrawArrays(GL_TRIANGLES, 0, vboData.size());

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
        double fps = 5;
        std::this_thread::sleep_for(std::chrono::milliseconds((int)(1000 / fps)));

        glfwSwapBuffers(window);
        glfwPollEvents();
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
    
    result.position.x += positionSpeed * keyTime(GLFW_KEY_D);
    result.position.x -= positionSpeed * keyTime(GLFW_KEY_A);
    result.position.y += positionSpeed * keyTime(GLFW_KEY_SPACE);
    result.position.y -= positionSpeed * keyTime(GLFW_KEY_LEFT_SHIFT);
    result.position.z += positionSpeed * keyTime(GLFW_KEY_W);
    result.position.z -= positionSpeed * keyTime(GLFW_KEY_S);

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