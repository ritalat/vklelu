#pragma once

#include "SDL.h"

#include "vulkan/vulkan.h"

#include <vector>

class VKlelu
{
public:
    VKlelu(int argc, char *argv[]);
    ~VKlelu();
    int run();

private:
    void draw();

    bool load_shader(const char *path, VkShaderModule &module);

    bool init_vulkan();
    bool init_swapchain();
    bool init_commands();
    bool init_default_renderpass();
    bool init_framebuffers();
    bool init_sync_structures();
    bool init_pipelines();

    int frameCount;
    int currentShader;

    SDL_Window *window;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkSurfaceKHR surface;
    VkQueue graphicsQueue;
    uint32_t graphicsQueueFamily;

    VkSwapchainKHR swapchain;
    VkFormat swapchainImageFormat;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;

    VkCommandPool commandPool;
    VkCommandBuffer mainCommandBuffer;

    VkRenderPass renderPass;

    std::vector<VkFramebuffer> framebuffers;

    VkSemaphore imageAcquiredSemaphore;
    VkSemaphore renderSemaphore;
    VkFence renderFence;

    VkPipeline trianglePipeline;
    VkPipeline rgbTrianglePipeline;
    VkPipelineLayout trianglePipelineLayout;
};
