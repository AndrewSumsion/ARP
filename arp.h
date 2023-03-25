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

namespace arp {

/**
 * Represents the position and orientation of a camera
 */
struct Pose {
    glm::vec3 position;
    glm::quat orientation;
};

/**
 * Texture swapchain that allows main thread to render while reprojection is
 * still accessing the last frame
 */
class Swapchain {
private:
    int index;
    bool* acquiredStatus;

    std::condition_variable cond;
    std::mutex mutex;

public:
    int width;
    int height;
    int numImages;
    std::uint32_t* images;

    Swapchain(int width, int height, int numImages);
    ~Swapchain();

    /**
     * Use this method to reserve an image on the swapchain for rendering.
     * Do not use an image in this swapchain without calling this first.
     * This method will block if there are no images available.
     */
    int acquireImage();

    /**
     * Used by reprojection when it is finished with an image.
     * The application should NOT use this, it will be done automatically.
     */
    void releaseImage(int index);
};

enum class FrameLayerFlags : std::uint32_t {
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
    std::vector<FrameLayer> layers;
};

/**
 * Function pointer type used by the application to specify how camera pose
 * responds to user input. This function MUST have no side effects, as it may
 * be called by reprojection many times for each frame
 * 
 * original - the last known position that was provided by arp
 * dx - the amount the mouse has moved since that position, x-dir in pixels
 * dy - the amount the mouse has moved since that position, ydir in pixels
 * dt - the amount of time that has passed since that position, in seconds
 * keys - a map of glfw key codes (ints) to the total amount of time (seconds
 *        as a double) that that key has been pressed since the last position
 */
typedef Pose (*PoseFunction)(const Pose& original, double dx, double dy,
                             double dt,
                             const std::unordered_map<int, double>& keys);

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
Pose getNextPose();

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