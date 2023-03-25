#include "arp.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>

using std::uint32_t;

namespace arp {

/// Forward declarations ///

static void appThreadStarter(ApplicationCallback callback);
static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
static void setupGL();
static void compileShader(GLuint shader, const char* source);

/// Reprojection variables ///

static bool initialized = false;

static PoseFunction poseFunction = nullptr;
static float projectionNear = -1;
static float projectionFar = -1;
static float projectionFovY = -1;
static float projectionAspect = -1;
static GLFWwindow* window = nullptr;
static GLFWwindow* hiddenWindow = nullptr;

static std::thread reprojectionThread;

static FrameSubmitInfo lastFrame;
static double lastFrameTime;

static Pose nextPose;

static bool cursorCaptured = false;
static double lastMouseX = 0;
static double lastMouseY = 0;
static std::unordered_map<int, double> keyTimes;
static std::unordered_map<int, double> keyPressTimes;
static std::unordered_set<int> pressedKeys;

/// Rendering variables ///

static const char* vertSrc =
    "#version 330 core\n"
    "in vec3 pos;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 mv;\n"
    "out float y;\n"
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

GLuint program;
glm::mat4 projection;
glm::mat4 view;

Swapchain::Swapchain(int width, int height, int numImages)
  : width(width), height(height), numImages(numImages), index(0)
{
    if(!initialized) {
        std::cout << "Error: Attempting to create swapchain before initialization!" << std::endl;
        return;
    }

    acquiredStatus = new bool[numImages];
    for(int i = 0; i < numImages; i++) {
        acquiredStatus[i] = false;
    }

    images = new uint32_t[numImages];
    glGenTextures(numImages, images);

    for(int i = 0; i < numImages; i++) {
        glBindTexture(GL_TEXTURE_2D, images[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    }
}

Swapchain::~Swapchain() {
    delete[] acquiredStatus;
    glDeleteTextures(numImages, images);
    delete[] images;
}

int Swapchain::acquireImage() {
    // block until image is not acquired
    if(acquiredStatus[index] == true) {
        std::unique_lock<std::mutex> lock(mutex);
        while(acquiredStatus[index] == true) {
            cond.wait(lock);
        }
        lock.unlock();
    }

    int i = index;
    acquiredStatus[i] = true;
    index = (index + 1) % numImages;
    return i;
}

void Swapchain::releaseImage(int i) {
    std::lock_guard<std::mutex> lock(mutex);
    acquiredStatus[i] = false;
    cond.notify_all();
}

int initialize() {
    if(!glfwGetCurrentContext()) {
        std::cout << "Error: cannot initialize ARP with no valid OpenGL context" << std::endl;
        return -1;
    }
    GLenum glewErr = glewInit();
    if(glewErr != GLEW_OK) {
        std::cout << "Error initializing GLEW: " << glewErr << std::endl;
        return -1;
    }
    initialized = true;
    return 0;
}

void registerPoseFunction(PoseFunction func) {
    poseFunction = func;
}

void captureCursor() {
    cursorCaptured = true;
}

void releaseCursor() {
    cursorCaptured = false;
}

void updateProjection(float near_, float far_, float fovY_, float aspectRatio_) {
    projectionNear = near_;
    projectionFar = far_;
    projectionFovY = fovY_;
    projectionAspect = aspectRatio_;

    // the projection matrix for reprojection must have a smaller near value
    // than the one used to render to keep the rendered plane at the correct
    // distance while also not clipping out of the view volume.
    // doing some geometry yields the following formula:
    //
    // reprojectionNear = renderNear * cos(fov / 2)
    // where fov is the max of fovX and fovY
    float fovY = projectionFovY;
    float fovX = projectionAspect * projectionFovY;

    float fov = fovY > fovX ? fovY : fovX;
    // I did my math for x and y axises independently, turns out a diagonal combination of both still clips
    // The math to properly figure this out is insane and involves a sphere so I'm just gonna fudge it
    // A constant multiple should work on all but very large fovs
    float near = projectionNear * cosf(fov / 2.f) * 0.5;
    float far = projectionFar;
    projection = glm::perspective(projectionFovY, projectionAspect, near, far);

    float xScale = projectionNear * tanf(fovX / 2.f);
    float yScale = projectionNear * tanf(fovY / 2.f);
    view = glm::translate(glm::mat4(1), glm::vec3(0, 0, -projectionNear)) *
           glm::scale(glm::mat4(1), glm::vec3(xScale, yScale, 1));
}

int startReprojection(ApplicationCallback callback) {
    if(!poseFunction) {
        std::cout << "Error: No pose function provided! Not starting reprojection." << std::endl;
        return -1;
    }

    window = glfwGetCurrentContext();

    nextPose.position = glm::vec3(0, 0, 0);
    nextPose.orientation = glm::quat(0, 0, 0, 1);
    lastFrame.pose = nextPose;

    // start app thread
    reprojectionThread = std::thread(appThreadStarter, callback);

    glfwSetKeyCallback(window, keyCallback);

    setupGL();

    while(!glfwWindowShouldClose(window)) {
        if(cursorCaptured) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        else {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        double time = glfwGetTime();
        for(int key : pressedKeys) {
            keyTimes[key] = time - keyPressTimes[key];
        }

        Pose lastPose = lastFrame.pose;
        // I'm gonna be honest I'm really confused about this part
        // This is getting the pose based on the difference from
        // the last submitted pose. I'm not sure if nextPose should
        // have something to do with this.
        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        double dx = mouseX - lastMouseX;
        double dy = mouseY - lastMouseY;
        double dt = glfwGetTime() - lastFrameTime;

        Pose renderPose = poseFunction(lastPose, dx, dy, dt, keyTimes);
        glm::mat4 mv = glm::mat4(renderPose.orientation) * view;
        glm::mat4 mvp = projection * mv;
        GLuint mvLoc = glGetUniformLocation(program, "mv");
        glUniformMatrix4fv(mvLoc, 1, GL_FALSE, &mv[0][0]);
        GLuint mvpLoc = glGetUniformLocation(program, "mvp");
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);

        // TODO: Draw the plane here

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glfwSwapBuffers(window);
        glfwPollEvents();

        // TODO: Remove. just for testing, cap the reprojection thread at 20fps
        //std::this_thread::sleep_for(std::chrono::milliseconds(1000 / 5));
    }

    glfwSetWindowShouldClose(hiddenWindow, GLFW_TRUE);

    return 0;
}

void submitFrame(const FrameSubmitInfo& submitInfo) {
    for(arp::FrameLayer& layer : lastFrame.layers) {
        layer.swapchain->releaseImage(layer.swapchainIndex);
    }

    lastFrame.layers = submitInfo.layers;

    // TODO: these should be the exact values provided to the pose function
    // This is a stopgap hoping they will be extremely close
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    double dx = mouseX - lastMouseX;
    double dy = mouseY - lastMouseY;
    double time = glfwGetTime();
    double dt = time - lastFrameTime;
    lastFrameTime = time;

    nextPose = poseFunction(nextPose, dx, dy, dt, keyTimes);
    lastMouseX = mouseX;
    lastMouseY = mouseY;

    keyTimes.clear();

    // reset key press times to current times to avoid key times going over frame time
    for(int key : pressedKeys) {
        keyPressTimes[key] = time;
    }
}

Pose getNextPose() {
    return nextPose;
}

void shutdown() {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
    reprojectionThread.join();
}

static void appThreadStarter(ApplicationCallback callback) {
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    hiddenWindow = glfwCreateWindow(1, 1, "", NULL, window);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwMakeContextCurrent(hiddenWindow);
    callback(hiddenWindow);

    // application has finished at this point
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if(action == GLFW_PRESS) {
        pressedKeys.insert(key);
        keyPressTimes[key] = glfwGetTime();
    }
    else if(action == GLFW_RELEASE) {
        pressedKeys.erase(key);
    }
}

static void setupGL() {
    // create VAO
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    float vertexData[] = {
        -1, -1, 0, // bottom left
        -1,  1, 0, // top left
         1, -1, 0, // bottom right
         1,  1, 0, // top right
    };

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertexData), vertexData, GL_STATIC_DRAW);

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    compileShader(vertexShader, vertSrc);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    compileShader(fragmentShader, fragSrc);
    
    program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    GLint isLinked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
    if(!isLinked) {
        GLint maxLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

        std::vector<GLchar> infoLog(maxLength);
        glGetProgramInfoLog(program, maxLength, &maxLength, infoLog.data());

        std::cout << "Error: failed to link program" << std::endl;
        std::cout << infoLog.data() << std::endl;
        return;
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint posLoc = glGetAttribLocation(program, "pos");
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);

}

static void compileShader(GLuint shader, const char* source) {
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint isCompiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if(!isCompiled) {
        GLint maxLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

        std::vector<GLchar> infoLog(maxLength);
        glGetShaderInfoLog(shader, maxLength, &maxLength, infoLog.data());

        std::cout << "Error: failed to compile shader" << std::endl;
        std::cout << infoLog.data() << std::endl;
    }
}

};