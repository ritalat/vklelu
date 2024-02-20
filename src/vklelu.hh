#pragma once

#include "context.hh"
#include "utils.hh"

#include "SDL.h"
#include "vk_mem_alloc.h"
#include "vulkan/vulkan.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define MAX_FRAMES_IN_FLIGHT 2

struct FrameData {
    VkCommandPool commandPool;
    VkCommandBuffer mainCommandBuffer;
    VkSemaphore imageAcquiredSemaphore;
    VkSemaphore renderSemaphore;
    VkFence renderFence;
    BufferAllocation cameraBuffer;
    BufferAllocation objectBuffer;
    VkDescriptorSet globalDescriptor;
    VkDescriptorSet objectDescriptor;
};

struct UploadContext {
    VkFence uploadFence;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
};

struct CameraData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewproj;
};

struct ObjectData {
    glm::mat4 model;
};

struct SceneData {
    glm::vec4 cameraPos;
    glm::vec4 lightPos;
    glm::vec4 lightColor;
};

class VKlelu
{
public:
    VKlelu(int argc, char *argv[]);
    ~VKlelu();
    int run();

private:
    void draw();
    void draw_objects(VkCommandBuffer cmd);
    void update();
    FrameData &get_current_frame();

    void set_runtime_dirs();
    void init_scene();
    Material *create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string &name);
    Mesh *get_mesh(const std::string &name);
    Material *get_material(const std::string &name);
    void load_meshes();
    void upload_mesh(Mesh &mesh);
    void load_images();
    bool load_image(const char *path, ImageAllocation &image);
    void immediate_submit(std::function<void(VkCommandBuffer cmad)> &&function);
    bool load_shader(const char *path, VkShaderModule &module);
    BufferAllocation create_buffer(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    ImageAllocation create_image(VkExtent3D extent, VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VmaMemoryUsage memoryUsage);
    size_t pad_uniform_buffer_size(size_t originalSize);

    bool init_vulkan();
    bool init_swapchain();
    bool init_commands();
    bool init_default_renderpass();
    bool init_framebuffers();
    bool init_sync_structures();
    bool init_descriptors();
    bool init_pipelines();

    std::string assetDir;
    std::string shaderDir;

    int frameCount;

    SDL_Window *window;
    VkExtent2D fbSize;

    std::unique_ptr<VulkanContext> ctx;

    VkSwapchainKHR swapchain;
    VkFormat swapchainImageFormat;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;

    ImageAllocation depthImage;
    VkImageView depthImageView;
    VkFormat depthImageFormat;

    VkRenderPass renderPass;
    std::vector<VkFramebuffer> framebuffers;

    VkDescriptorPool descriptorPool;
    VkDescriptorSetLayout globalSetLayout;
    VkDescriptorSetLayout objectSetLayout;
    VkDescriptorSetLayout singleTextureSetLayout;

    VkPipeline meshPipeline;
    VkPipelineLayout meshPipelineLayout;

    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> frameData;
    SceneData sceneParameters;
    BufferAllocation sceneParameterBuffer;
    UploadContext uploadContext;
    VkSampler linearSampler;

    std::vector<Himmeli> himmelit;
    std::unordered_map<std::string, Mesh> meshes;
    std::unordered_map<std::string, Material> materials;
    std::unordered_map<std::string, Texture> textures;

    std::vector<std::function<void()>> resourceJanitor;
};
