#ifndef ARP_H
#define ARP_H

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <unordered_map>
#include <cstdint>

/**
 * Example program:
 * 
 * // setup window
 * GLFWwindow* window = ...;
 * 
 * arp::Swapchain swapchain(1920, 1080, 3);
 * arp::updateProjection(0.1, 1000);
 * arp::startReprojection(window);
 * arp::captureCursor();
 * 
 * // arp will forward close events to hidden window
 * while (!glfwWindowShouldClose(window)) {
 *     int swapchainIndex = swapchain.acquireImage();
 *     GLuint texture = swapchain.images[swapchainIndex];
 *     Pose pose = arp::getNextPose();
 *     
 *     // render to texture with view determined by pose ...
 * 
 *     FrameLayer layers[] = {{0}};
 *     layers[0].pose = pose;
 *     layers[0].fov = fov;
 *     layers[0].flags = PARALLAX_ENABLED;
 *     layers[0].swapchain = swapchain;
 *     layers[0].swapchainIndex = swapchainIndex;
 * 
 *     FrameSubmitInfo submitInfo;
 *     submitInfo.numLayers = 1;
 *     submitInfo.layers = layers;
 * 
 *     arp::submitFrame(submitInfo);
 * }
 * 
 * arp::releaseCursor();
 * 
 */

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
    Pose pose;
    double fov;
    FrameLayerFlags flags;

    Swapchain& swapchain;
    int swapchainIndex;
};

struct FrameSubmitInfo {
    int numLayers;
    FrameLayer* layers;
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
int startReprojection(GLFWwindow*& window);

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
void updateProjection(float near, float far);

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