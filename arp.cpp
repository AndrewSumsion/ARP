#include "arp.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "glm/glm.hpp"

#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <cmath>

using std::uint32_t;

namespace arp {

/// Forward declarations ///

static void appThreadStarter(ApplicationCallback callback);
static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
static void setupGL();
static void drawLayer(const FrameLayer& layer);
static void drawLayerParallaxEnabled(const FrameLayer& layer);
static GLuint compileProgram(const char* vertShaderSrc, const char* fragShaderSrc);
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

static std::mutex posesMutex;
static bool frameValid = false;
static FrameSubmitInfo lastFrame;

// used for prediction
static const int historySize = 10;
static std::list<PoseInfo> poseHistory;

static Pose cameraPose;
static PoseInfo cameraPoseInfo{0};

static bool cursorCaptured = false;
static std::mutex keyTimesMutex;
static std::unordered_map<int, double> keyTimes;
static std::unordered_set<int> pressedKeys;

static GLFWkeyfun originalKeyCallback;
static GLFWframebuffersizefun originalFramebufferSizeCallback;

/// Rendering variables ///

static const char* vertSrc =
    "#version 330 core\n"
    "layout(location = 0) in vec3 pos;\n"
    "uniform mat4 model;\n"
    "uniform mat4 view;\n"
    "uniform mat4 projection;\n"
    "out vec2 texCoords;\n"
    "void main() {\n"
    "    gl_Position = projection * view * model * vec4(pos, 1);\n"
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

/**
 * Uniforms that need to be set:
 * model - transform the radius-1 plane to last frame far plane
 * view - current camera pose view
 * projection - projection with extended far to fit plane
 * cameraPos - current camera translation in world space
 * frameView - the view matrix the last frame was rendered with
 * frameProjection - the projection matrix the last frame was rendered with
 * tex - color texture of last frame
 * depthTex - depth texture of last frame
 */
static const char* parallaxVertSrc =
    "#version 330 core\n"
    "layout(location = 1) in vec3 pos;\n"
    "uniform mat4 model;\n"
    "uniform mat4 view;\n"
    "uniform mat4 projection;\n"
    "uniform vec3 cameraPos;\n"
    "out vec3 cameraToFrag;\n"
    "void main() {\n"
    "    gl_Position = projection * view * model * vec4(pos, 1);\n"
    "    cameraToFrag = (model * vec4(pos, 1)).xyz - cameraPos;\n"
    "}\n"
    ;

static const char* parallaxFragSrc =
    "#version 330 core\n"
    "\n"
    "#define MAX_PARALLAX_ITERATIONS 64\n"
    "layout(location = 0) out vec4 color;\n"
    "uniform sampler2D tex;\n"
    "uniform sampler2D depthTex;\n"
    "uniform mat4 frameView;\n"
    "uniform mat4 frameProjection;\n"
    "uniform vec3 cameraPos;\n"
    "in vec3 cameraToFrag;\n"
    "\n"
    "bool insideDepthMap(vec3 pos) {\n"
    "    vec4 posProj = frameProjection * frameView * vec4(pos, 1);\n"
    "    vec3 depthCoords = posProj.xyz / posProj.w;\n"
    "    depthCoords = depthCoords * 0.5 + 0.5;\n"
    "    return texture(depthTex, depthCoords.xy).r < depthCoords.z;\n"
    "}\n"
    "    \n"
    "void main() {\n"
    "    vec3 pos = cameraPos;\n"
    "    for(int i = 0; i < MAX_PARALLAX_ITERATIONS; i++) {\n"
    "        pos = cameraPos + ((float(i) + 1.0) / MAX_PARALLAX_ITERATIONS) * cameraToFrag;\n"
    "        if(insideDepthMap(pos))\n"
    "            break;\n"
    "    }\n"
    "\n"
    "    //color = vec4(1 - length(pos - cameraPos) / length(cameraToFrag), 0, 0, 1);\n"
    "    vec4 posProj = frameProjection * frameView * vec4(pos, 1);\n"
    "    vec2 texCoords = (posProj.xy / posProj.w) * 0.5 + 0.5;\n"
    "    color = texture(tex, texCoords);\n"
    "}"
    ;

GLuint defaultProgram;
GLuint parallaxProgram;
glm::mat4 projection;

Swapchain::Swapchain(int width, int height, int numImages)
  : width(width),
    height(height),
    numImages(numImages),
    index(0),
    acquiredStatus(numImages),
    fbos(numImages),
    images(numImages),
    depthImages(numImages)
{
    if(!initialized) {
        std::cout << "Error: Attempting to create swapchain before initialization!" << std::endl;
        return;
    }

    for(int i = 0; i < numImages; i++) {
        acquiredStatus[i] = 0;
    }

    glGenTextures(numImages, images.data());
    for(int i = 0; i < numImages; i++) {
        glBindTexture(GL_TEXTURE_2D, images[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glGenTextures(numImages, depthImages.data());
    for(int i = 0; i < numImages; i++) {
        glBindTexture(GL_TEXTURE_2D, depthImages[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    GLuint originalFramebuffer;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, (GLint*)&originalFramebuffer);

    glGenFramebuffers(numImages, fbos.data());
    for(int i = 0; i < numImages; i++)  {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[i]);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, images[i], 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthImages[i], 0);
        if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cout << "Creating swapchain: Framebuffer incomplete! ";
            std::cout << glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) << std::endl;
        }
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, originalFramebuffer);
}

Swapchain::~Swapchain() {
    glDeleteTextures(numImages, images.data());
    glDeleteTextures(numImages, depthImages.data());
    glDeleteFramebuffers(numImages, fbos.data());
}

void Swapchain::createTextures() {

}

int Swapchain::acquireImage() {
    // block until image is not acquired
    if(acquiredStatus[index]) {
        std::unique_lock<std::mutex> lock(mutex);
        while(acquiredStatus[index]) {
            cond.wait(lock);
        }
        lock.unlock();
    }

    int i = index;
    acquiredStatus[i] = 1;
    index = (index + 1) % numImages;
    return i;
}

void Swapchain::bindFramebuffer(int index) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbos[index]);
}

void Swapchain::resize(int newWidth, int newHeight) {
    std::lock_guard<std::mutex> lock(mutex);
    this->width = newWidth;
    this->height = newHeight;
    for(int i = 0; i < numImages; i++) {
        glBindTexture(GL_TEXTURE_2D, images[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, depthImages[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
    }
}

void Swapchain::releaseImage(int i) {
    std::lock_guard<std::mutex> lock(mutex);
    acquiredStatus[i] = 0;
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

    projection = glm::perspective(projectionFovY, projectionAspect, projectionNear, projectionFar * 2);
}

int startReprojection(ApplicationCallback callback) {
    if(!poseFunction) {
        std::cout << "Error: No pose function provided! Not starting reprojection." << std::endl;
        return -1;
    }

    window = glfwGetCurrentContext();
    glfwSwapInterval(1);

    cameraPose.position = glm::vec3(0, 0, 0);
    cameraPose.orientation = glm::quat(1, 0, 0, 0);
    lastFrame.pose = cameraPose;

    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    hiddenWindow = glfwCreateWindow(1, 1, "", NULL, window);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    // start app thread
    reprojectionThread = std::thread(appThreadStarter, callback);

    originalKeyCallback = glfwSetKeyCallback(window, keyCallback);
    originalFramebufferSizeCallback = glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

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
        {
            std::lock_guard<std::mutex> keyTimesLock(keyTimesMutex);
            std::lock_guard<std::mutex> posesLock(posesMutex);
            for(int key : pressedKeys) {
                keyTimes[key] += lastFrameDuration;
            }
        

            // submitFrame sets last* variables
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);
            cameraPoseInfo.mouseX = mouseX;
            cameraPoseInfo.mouseY = mouseY;
            cameraPoseInfo.time = time;

            double dx = cameraPoseInfo.mouseX - lastFrame.poseInfo.mouseX;
            double dy = cameraPoseInfo.mouseY - lastFrame.poseInfo.mouseY;
            double dt = cameraPoseInfo.time   - lastFrame.poseInfo.time;

            if(!cursorCaptured) {
                dx = 0;
                dy = 0;
            }

            cameraPose = poseFunction(lastFrame.poseInfo.realPose, dx, dy, dt, keyTimeFunction);
            cameraPoseInfo.realPose = cameraPose;
            
            // orientationDifference: camera - lastFrame

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            if(frameValid)
                for(int i = lastFrame.layers.size() - 1; i >= 0; i--)
                    drawLayer(lastFrame.layers[i]);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwSetWindowShouldClose(hiddenWindow, GLFW_TRUE);

    reprojectionThread.join();

    return 0;
}

static void drawLayer(const FrameLayer& layer) {
    if((layer.flags & PARALLAX_ENABLED) && !(layer.flags & CAMERA_LOCKED) &&
        lastFrame.pose.position != cameraPose.position) {
        drawLayerParallaxEnabled(layer);
        return;
    }
    float fovY = layer.fov;
    // TODO: allow layers with different aspect ratios
    float yScale = projectionFar * tanf(fovY / 2.f);
    float xScale = projectionAspect * yScale;

    glm::mat4 scale = glm::scale(glm::mat4(1), glm::vec3(xScale, yScale, 1));
    glm::mat4 farPlaneOffset = glm::translate(glm::mat4(1), glm::vec3(0, 0, -projectionFar));
    glm::mat4 translation = glm::translate(glm::mat4(1), lastFrame.pose.position);
    glm::mat4 rotation;
    if(!(layer.flags & CAMERA_LOCKED)) {
        rotation = glm::mat4(lastFrame.pose.orientation);
    }
    else {
        rotation = glm::mat4(cameraPose.orientation);
    }

    glm::mat4 model = translation * rotation * farPlaneOffset * scale;

    glm::mat4 camera = glm::translate(glm::mat4(1), lastFrame.pose.position) * glm::mat4(cameraPose.orientation);
    glm::mat4 view = glm::inverse(camera);

    glUseProgram(defaultProgram);

    // setup uniforms
    GLuint modelLoc = glGetUniformLocation(defaultProgram, "model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &model[0][0]);
    GLuint viewLoc = glGetUniformLocation(defaultProgram, "view");
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &view[0][0]);
    GLuint projLoc = glGetUniformLocation(defaultProgram, "projection");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, &projection[0][0]);

    // draw quad
    GLuint texture = layer.swapchain->images[layer.swapchainIndex];
    glBindTexture(GL_TEXTURE_2D, texture);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void drawLayerParallaxEnabled(const FrameLayer& layer) {
    // setup transformation matrices
    float fovY = layer.fov;
    float yScale = projectionFar * tanf(fovY / 2.f);
    float xScale = projectionAspect * yScale;

    glm::mat4 scale = glm::scale(glm::mat4(1), glm::vec3(xScale, yScale, 1));
    glm::mat4 farPlaneOffset = glm::translate(glm::mat4(1), glm::vec3(0, 0, -projectionFar));
    glm::mat4 translation = glm::translate(glm::mat4(1), lastFrame.pose.position);
    glm::mat4 rotation = glm::mat4(lastFrame.pose.orientation);

    glm::mat4 model = translation * rotation * farPlaneOffset * scale;

    glm::mat4 camera = glm::translate(glm::mat4(1), cameraPose.position) * glm::mat4(cameraPose.orientation);
    glm::mat4 view = glm::inverse(camera);

    glm::mat4 frameCamera = glm::translate(glm::mat4(1), lastFrame.pose.position) * glm::mat4(lastFrame.pose.orientation);
    glm::mat4 frameView = glm::inverse(frameCamera);
    glm::mat4 frameProjection = glm::perspective(projectionFovY, projectionAspect, projectionNear, projectionFar);

    // update uniforms
    glUseProgram(parallaxProgram);

    GLuint modelLoc = glGetUniformLocation(parallaxProgram, "model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &model[0][0]);
    GLuint viewLoc = glGetUniformLocation(parallaxProgram, "view");
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &view[0][0]);
    GLuint projectionLoc = glGetUniformLocation(parallaxProgram, "projection");
    glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, &projection[0][0]);
    GLuint cameraPosLoc = glGetUniformLocation(parallaxProgram, "cameraPos");
    glUniform3fv(cameraPosLoc, 1, &cameraPose.position[0]);
    GLuint frameViewLoc = glGetUniformLocation(parallaxProgram, "frameView");
    glUniformMatrix4fv(frameViewLoc, 1, GL_FALSE, &frameView[0][0]);
    GLuint frameProjectionLoc = glGetUniformLocation(parallaxProgram, "frameProjection");
    glUniformMatrix4fv(frameProjectionLoc, 1, GL_FALSE, &frameProjection[0][0]);
    GLuint texLoc = glGetUniformLocation(parallaxProgram, "tex");
    glUniform1i(texLoc, 0);
    GLuint depthTexLoc = glGetUniformLocation(parallaxProgram, "depthTex");
    glUniform1i(depthTexLoc, 1);

    // bind textures
    GLuint tex = layer.swapchain->images[layer.swapchainIndex];
    GLuint depthTex = layer.swapchain->depthImages[layer.swapchainIndex];

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, depthTex);
    glActiveTexture(GL_TEXTURE0);

    // draw
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void submitFrame(const FrameSubmitInfo& submitInfo) {
    glFlush();

    // keep cycling list of historySize last frames
    poseHistory.push_back(submitInfo.poseInfo);
    while(poseHistory.size() > historySize) {
        poseHistory.pop_front();
    }

    for(arp::FrameLayer& layer : lastFrame.layers) {
        layer.swapchain->releaseImage(layer.swapchainIndex);
    }

    {
        std::lock_guard<std::mutex> posesLock(posesMutex);
        lastFrame = submitInfo;
    }

    {
        std::lock_guard<std::mutex> lock(keyTimesMutex);
        keyTimes.clear();
    }

    frameValid = true;
}

void getCameraPose(Pose& pose, PoseInfo& poseInfo) {
    std::lock_guard<std::mutex> posesLock(posesMutex);
    pose = cameraPose;
    poseInfo = cameraPoseInfo;
}

double getPredictedDisplayTime() {
    if(poseHistory.size() < 2) {
        // no way to guess if the history is empty, just return 60fps interval
        return 1.0 / 60.0;
    }

    // calculate average interval over frame history
    int numIntervals = poseHistory.size() - 1;
    double lastTime = -1.0;
    double intervalsTotal = 0;
    for(const PoseInfo& poseInfo : poseHistory) {
        if(lastTime == -1.0) {
            lastTime = poseInfo.time;
            continue;
        }

        intervalsTotal += poseInfo.time - lastTime;
        lastTime = poseInfo.time;
    }
    return lastFrame.poseInfo.time + intervalsTotal / numIntervals;
}

static std::mutex predictionMutex;
static double predictedDt = 0;
void getPredictedCameraPose(double time, Pose& pose, PoseInfo& poseInfo) {
    std::lock_guard<std::mutex> posesLock(posesMutex);
    poseInfo = cameraPoseInfo;
    
    // halving these differences to put prediction between last and next frame
    double dt = time - glfwGetTime();
    dt *= 0.5;

    // assume the mouse will continue most recent movement
    double dx = cameraPoseInfo.mouseX - lastFrame.poseInfo.mouseX;
    double dy = cameraPoseInfo.mouseY - lastFrame.poseInfo.mouseY;
    dx *= 0.5;
    dy *= 0.5;

    // unfortunately can't get away with doing this without a global variable
    // so we set a global variable and lock this portion to make thread safe
    std::lock_guard<std::mutex> lock(predictionMutex);
    predictedDt = dt;

    pose = poseFunction(cameraPose, dx, dy, dt, [](int key){
        return pressedKeys.count(key) ? predictedDt : 0.0;
    });
}

void shutdown() {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
    reprojectionThread.join();
}

static void appThreadStarter(ApplicationCallback callback) {
    glfwMakeContextCurrent(hiddenWindow);
    callback(hiddenWindow);

    // application has finished at this point
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if(action == GLFW_PRESS) {
        pressedKeys.insert(key);
    }
    else if(action == GLFW_RELEASE) {
        pressedKeys.erase(key);
    }

    if(originalKeyCallback)
        originalKeyCallback(window, key, scancode, action, mods);
}

static void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    if(originalFramebufferSizeCallback) {
        originalFramebufferSizeCallback(window, width, height);
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
    
    defaultProgram = compileProgram(vertSrc, fragSrc);

    GLint posLoc = glGetAttribLocation(defaultProgram, "pos");
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);

    parallaxProgram = compileProgram(parallaxVertSrc, parallaxFragSrc);
    posLoc = glGetAttribLocation(parallaxProgram, "pos");
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);

}

static GLuint compileProgram(const char* vertShaderSrc, const char* fragShaderSrc) {
    GLuint program = glCreateProgram();

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    compileShader(vertexShader, vertShaderSrc);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    compileShader(fragmentShader, fragShaderSrc);

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
        return 0;
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
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
