#include "arp.h"
#include <GL/glew.h>
#include <cyGL.h>
#include <cyTriMesh.h>

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
    const arp::Pose& original, double dx, double dy,
    double dt,
    arp::KeyTimeFunction keyTime);

static void appCallback(GLFWwindow* window);

static const char* meshPath;

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

    if(arp::initialize() != 0) {
        std::cout << "Unable to initialize arp" << std::endl;
        return -1;
    }

    arp::registerPoseFunction(poseFunction);
    arp::updateProjection(0.1, 100, 90 * 3.14159265358979324 / 180, 1);
    arp::startReprojection(appCallback);

    // arp has taken over this thread and blocks until program is over
}

static void appCallback(GLFWwindow* window) {
    arp::Swapchain swapchain(640, 480, 3);
    GLuint fbos[3];
    for(int i = 0; i < 3; i++) {
        glGenFramebuffers(1, &fbos[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, swapchain.images[i], 0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	        std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
    }

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

    glViewport(0, 0, swapchain.width, swapchain.height);

    arp::captureCursor();

    while(!glfwWindowShouldClose(window)) {
        int swapchainIndex = swapchain.acquireImage();
        GLuint texture = swapchain.images[swapchainIndex];
        glBindTexture(GL_TEXTURE_2D, texture);
        arp::Pose pose = arp::getNextPose();
        //std::cout << "x: " << pose.position.x << " y: " << pose.position.y << " z: " << pose.position.z << std::endl;

        glm::vec3 euler = glm::eulerAngles(pose.orientation);
        //std::cout << "x: " << euler.x << " y: " << euler.y << " z: " << euler.z << std::endl;

        glBindFramebuffer(GL_FRAMEBUFFER, fbos[swapchainIndex]);
        glClearColor(0, 0.5, 0.5, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 projection = glm::perspective(0.5f * 3.14159265358979324f, 1.f, 0.1f, 100.f);
        glm::mat4 camera = glm::translate(glm::mat4(1), pose.position) * glm::mat4(pose.orientation);
        glm::mat4 view = glm::inverse(camera);
        glm::mat4 mvp = projection * view;

        program.SetUniformMatrix4("mvp", &mvp[0][0]);

        program.Bind();
        glDrawArrays(GL_TRIANGLES, 0, vboData.size());

        arp::FrameSubmitInfo submitInfo;
        submitInfo.pose = pose;

        arp::FrameLayer layer;
        layer.flags = arp::FrameLayerFlags::NONE;
        layer.fov = 3.14159265358979324 / 2;
        layer.swapchain = &swapchain;
        layer.swapchainIndex = swapchainIndex;

        submitInfo.layers.push_back(layer);

        arp::submitFrame(submitInfo);
        double fps = 0.5;
        std::this_thread::sleep_for(std::chrono::milliseconds((int)(1000 / fps)));

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    arp::releaseCursor();
}

static double positionSpeed = 10;
static double rotationSpeed = -0.001;

static arp::Pose poseFunction(
    const arp::Pose& original, double dx, double dy,
    double dt,
    arp::KeyTimeFunction keyTime) {

    arp::Pose result;
    glm::quat qX = glm::normalize(glm::angleAxis((float)(rotationSpeed * dy), glm::vec3(1, 0, 0)));
    glm::quat qY = glm::normalize(glm::angleAxis((float)(rotationSpeed * dx), glm::vec3(0, 1, 0)));
    result.orientation = glm::normalize(qX * qY * original.orientation);

    result.position = original.position;
    
    result.position.x += positionSpeed * keyTime(GLFW_KEY_D);
    result.position.x -= positionSpeed * keyTime(GLFW_KEY_A);
    result.position.y += positionSpeed * keyTime(GLFW_KEY_SPACE);
    result.position.y -= positionSpeed * keyTime(GLFW_KEY_LEFT_SHIFT);
    result.position.z += positionSpeed * keyTime(GLFW_KEY_W);
    result.position.z -= positionSpeed * keyTime(GLFW_KEY_S);

    return result;
}