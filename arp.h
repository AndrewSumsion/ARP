#ifndef ARP_H
#define ARP_H

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <unordered_map>
#include <cstdint>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace arp {

/**
 * Represents the position and orientation of a camera
 *
 * If you would like to specify custom info to track with poses (such as pitch/
 * yaw), define a struct called PoseData that is less than 64 bytes in size and
 * define ARP_CUSTOM_POSE_DATA before including
 */
struct Pose {
    union {
        unsigned char dataRaw[64];
        #ifdef ARP_CUSTOM_POSE_DATA
        static_assert(sizeof(PoseData) <= 64, "Custom PoseData must be smaller than 64 bytes");
        PoseData data;
        #endif
    };
    glm::vec3 position;
    glm::quat orientation;
};

/**
 * Contains absolute input info that was used to generate a pose
 */
struct PoseInfo {
    double mouseX;
    double mouseY;
    double time;

    // used for predicted poses.
    // if prediction is not used, ignore this
    Pose realPose;
};

/**
 * Texture swapchain that allows main thread to render while reprojection is
 * still accessing the last frame
 */
class Swapchain {
private:
    int index;
    std::vector<std::uint8_t> acquiredStatus;
    std::vector<std::uint32_t> fbos;

    std::condition_variable cond;
    std::mutex mutex;

    void createTextures();

public:
    int width;
    int height;
    int numImages;
    std::vector<std::uint32_t> images;
    std::vector<std::uint32_t> depthImages;

    Swapchain(int width, int height, int numImages);
    ~Swapchain();

    /**
     * Use this method to reserve an image on the swapchain for rendering.
     * Do not use an image in this swapchain without calling this first.
     * This method will block if there are no images available.
     */
    int acquireImage();

    /**
     * This method binds the framebuffer to draw on the image of the specified
     * index and sets the glViewport accordingly
     */
    void bindFramebuffer(int index);

    /**
     * Resizes the texture images in the swapchain
     */
    void resize(int width, int height);

    /**
     * Used by reprojection when it is finished with an image.
     * The application should NOT use this, it will be done automatically.
     */
    void releaseImage(int index);
};

enum FrameLayerFlags : std::uint32_t {
    NONE = 0,
    // Changes in position are approximated by parallax mapping
    PARALLAX_ENABLED = 1 << 0,
    // Layer is not reprojected, always drawn in screen space
    CAMERA_LOCKED = 1 << 1,
};

struct FrameLayer {
    double fov;
    FrameLayerFlags flags;

    Swapchain* swapchain;
    int swapchainIndex;
};

struct FrameSubmitInfo {
    Pose pose;
    PoseInfo poseInfo;
    std::vector<FrameLayer> layers;
};

/**
 * Function pointer type used by PoseFunction. Returns the amount of time the
 * given key has been pressed since the last frame
 */
typedef double (*KeyTimeFunction)(int key);

/**
 * Function pointer type used by the application to specify how camera pose
 * responds to user input. This function MUST have no side effects, as it may
 * be called by reprojection many times for each frame
 *
 * mouseX - Net movement of mouse in the X direction. A value of zero does not
 *          necessarily correspond to any specific position
 * mouseY - Net movement of mouse in the Y direction
 * time - A monotonically increasing timer in seconds. Note that the value may
 *        not be monotonic from the perspective of this function, as this
 *        function may be used for different times in any order. The time
 *        should never be before the most recently submitted frame.
 * keyTime - A function that gets the total time a certain key (GLFW int key
 *           code) has been pressed, returned as a double in seconds
 *
 * Returns the position and orientation of the camera with the given input
 */
typedef Pose (*PoseFunction)(const Pose& lastPose, double dx, double dy,
                             double dt, KeyTimeFunction keyTime);

/**
 * Applications should implement their main loops in this thread. When
 * startReprojection is called, reprojection takes over the main thread, and
 * the application must run on a secondary thread. The provided window argument
 * is a hidden window with a shared context with the main window.
 */
typedef void (*ApplicationCallback)(GLFWwindow* window);

/**
 * Inititalizes the ARP library. Returns 0 on success
 */
int initialize();

/**
 * After creating a GLFW window, this function hands it off to the reprojection
 * thread. The window will be replaced with with a hidden window that shares a
 * context with the original window. Use this window for rendering.
 *
 * This new window is meant to be essentially an offscreen context. The default
 * framebuffer should not be drawn to, as it will have no effect. Only FBOs
 * should be used in this context.
 *
 * Returns 0 on success
 */
int startReprojection(ApplicationCallback callback);

/**
 * Registers the function used to determine poses by input
 */
void registerPoseFunction(PoseFunction function);

/**
 * Causes the main window to capture the mouse cursor
 */
void captureCursor();

/**
 * Causes the main window to release the mouse cursor
 */
void releaseCursor();

/**
 * Specify features of the projection
 * The reprojection needs to know this to accurately reproject the scene
 */
void updateProjection(float near, float far, float fovY, float aspectRatio);

/**
 * Returns the pose that should be used to render the next frame
 */
void getCameraPose(Pose& pose, PoseInfo& poseInfo);

/**
 * Returns the estimated time that the next frame will be submitted based on
 * previous frames
 */
double getPredictedDisplayTime();

/**
 * Returns the pose that should be used to render the next frame
 * 
 * ARP will attempt to predict where the user-controlled pose will be by the
 * time rendering is done. 
 */
void getPredictedCameraPose(double time, Pose& pose, PoseInfo& poseInfo);

/**
 * Submits frame
 */
void submitFrame(const FrameSubmitInfo& submitInfo);

/**
 * Stops the reprojection thread and cleans up any resources used.
 */
void shutdown();

};

#endif // ARP_H
