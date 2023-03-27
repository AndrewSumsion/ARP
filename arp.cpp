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
static double keyTimeFunction(int key);

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

static bool frameValid = false;
static FrameSubmitInfo lastFrame;

static Pose cameraPose;

static bool cursorCaptured = false;
static std::unordered_map<int, double> keyTimes;
static std::unordered_set<int> pressedKeys;

/// Rendering variables ///

static const char* vertSrc =
    "#version 330 core\n"
    "in vec3 pos;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 mv;\n"
    "out vec2 texCoords;\n"
    "void main() {\n"
    "    gl_Position = mvp * vec4(pos, 1);\n"
    "    texCoords = (pos.xy + vec2(1, 1)) * 0.5;\n"
    "}\n"
    ;

static const char* fragSrc =
    "#version 330 core\n"
    "layout(location = 0) out vec4 color;\n"
    "in vec2 texCoords;\n"
    "uniform sampler2D tex;\n"
    "void main() {\n"
    "    color = texture(tex, texCoords);\n"
    "    //color = vec4(texCoords, 0, 1);\n"
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
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);  
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

    cameraPose.position = glm::vec3(0, 0, 0);
    cameraPose.orientation = glm::quat(1, 0, 0, 0);
    lastFrame.pose = cameraPose;

    // start app thread
    reprojectionThread = std::thread(appThreadStarter, callback);

    glfwSetKeyCallback(window, keyCallback);

    setupGL();

    // absolute time value used for tracking frame time
    double frameStartTime = glfwGetTime();

    while(!glfwWindowShouldClose(window)) {
        if(cursorCaptured) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        else {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        double time = glfwGetTime();
        double lastFrameDuration = time - frameStartTime;
        frameStartTime = time;
        for(int key : pressedKeys) {
            keyTimes[key] += lastFrameDuration;
        }

        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        cameraPose = poseFunction(mouseX, mouseY, time, keyTimeFunction);
        // orientationDifference: camera - lastFrame
        glm::quat orienationDifference = glm::inverse(lastFrame.pose.orientation) * cameraPose.orientation;
        glm::mat4 mv = glm::mat4(glm::inverse(orienationDifference)) * view;
        glm::mat4 mvp = projection * mv;
        GLuint mvLoc = glGetUniformLocation(program, "mv");
        glUniformMatrix4fv(mvLoc, 1, GL_FALSE, &mv[0][0]);
        GLuint mvpLoc = glGetUniformLocation(program, "mvp");
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);


        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);
        // TODO: Properly support layers
        if(frameValid) {
            GLuint texture = lastFrame.layers[0].swapchain->images[lastFrame.layers[0].swapchainIndex];
            glBindTexture(GL_TEXTURE_2D, texture);
        }
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
    glFlush();

    for(arp::FrameLayer& layer : lastFrame.layers) {
        layer.swapchain->releaseImage(layer.swapchainIndex);
    }

    lastFrame = submitInfo;

    frameValid = true;
}

Pose getCameraPose() {
    return cameraPose;
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
    if(action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) {
        cursorCaptured = !cursorCaptured;
        return;
    }

    if(action == GLFW_PRESS) {
        pressedKeys.insert(key);
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

static double keyTimeFunction(int key) {
    return keyTimes[key];
}

};