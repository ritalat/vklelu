#pragma once

#include "utils.hh"

#include "SDL.h"
#include "vk_mem_alloc.h"
#include "vulkan/vulkan.h"

#include <string>
#include <unordered_map>
#include <vector>

class VKlelu
{
public:
    VKlelu(int argc, char *argv[]);
    ~VKlelu();
    int run();

private:
    void draw();
    void draw_objects(VkCommandBuffer cmd, Himmeli *first, int count);

    bool wd_is_builddir();

    void init_scene();
    void load_meshes();
    void upload_mesh(Mesh &mesh);
    bool load_shader(const char *path, VkShaderModule &module);
    Material *create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string &name);
    Mesh *get_mesh(const std::string &name);
    Material *get_material(const std::string &name);

    bool init_vulkan();
    bool init_swapchain();
    bool init_commands();
    bool init_default_renderpass();
    bool init_framebuffers();
    bool init_sync_structures();
    bool init_pipelines();

    int frameCount;

    std::vector<Himmeli> himmelit;
    std::unordered_map<std::string, Mesh> meshes;
    std::unordered_map<std::string, Material> materials;

    SDL_Window *window;
    VkExtent2D fbSize;

    VmaAllocator allocator;

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

    ImageAllocation depthImage;
    VkImageView depthImageView;
    VkFormat depthImageFormat;

    VkCommandPool commandPool;
    VkCommandBuffer mainCommandBuffer;

    VkRenderPass renderPass;
    std::vector<VkFramebuffer> framebuffers;

    VkSemaphore imageAcquiredSemaphore;
    VkSemaphore renderSemaphore;
    VkFence renderFence;
};
