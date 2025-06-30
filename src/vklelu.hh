#pragma once

#include "context.hh"
#include "himmeli.hh"
#include "memory.hh"
#include "utils.hh"

#include "SDL3/SDL.h"
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
    VkFence renderFence;
    std::unique_ptr<BufferAllocation> cameraBuffer;
    void *cameraBufferMapping;
    std::unique_ptr<BufferAllocation> objectBuffer;
    void *objectBufferMapping;
    VkDescriptorSet globalDescriptor;
    VkDescriptorSet objectDescriptor;
};

struct SwapchainData {
    VkSemaphore renderSemaphore;
    VkImage image;
    VkImageView imageView;
};

struct UploadContext {
    VkFence uploadFence;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
};

struct CameraData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewProj;
};

struct ObjectData {
    glm::mat4 model;
    glm::mat4 normalMat;
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
    void update();
    void draw();
    void drawObjects(VkCommandBuffer cmd);
    FrameData &getCurrentFrame();

    void initScene();
    Material *createMaterial(VkPipeline pipeline, VkPipelineLayout layout, const std::string name);
    Mesh *getMesh(const std::string name);
    Material *getMaterial(const std::string name);
    void uploadMesh(ObjFile &obj, std::string name);
    void uploadImage(ImageFile &image, std::string name);
    void immediateSubmit(std::function<void(VkCommandBuffer)> &&function);
    void loadShader(const char *path, VkShaderModule &module);
    void deferCleanup(std::function<void()> &&cleanupFunc);

    void initVulkan();
    void initSwapchain();
    void initCommands();
    void initSyncStructures();
    void initDescriptors();
    void initPipelines();

    int m_frameCount;

    SDL_Window *m_window;
    VkExtent2D m_fbSize;

    std::unique_ptr<VulkanContext> m_ctx;
    VkDevice m_device;

    VkSwapchainKHR m_swapchain;
    VkFormat m_swapchainImageFormat;
    std::vector<SwapchainData> m_swapchainData;

    Texture m_depthImage;
    VkFormat m_depthImageFormat;

    VkDescriptorPool m_descriptorPool;
    VkDescriptorSetLayout m_globalSetLayout;
    VkDescriptorSetLayout m_objectSetLayout;
    VkDescriptorSetLayout m_singleTextureSetLayout;

    VkPipeline m_meshPipeline;
    VkPipelineLayout m_meshPipelineLayout;

    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> m_frameData;
    SceneData m_sceneParameters;
    std::unique_ptr<BufferAllocation> m_sceneParameterBuffer;
    void *m_sceneParameterBufferMapping;
    UploadContext m_uploadContext;
    VkSampler m_linearSampler;

    std::vector<Himmeli> m_himmelit;
    std::unordered_map<std::string, Mesh> m_meshes;
    std::unordered_map<std::string, Material> m_materials;
    std::unordered_map<std::string, Texture> m_textures;

    std::vector<std::function<void()>> m_resourceJanitor;
};
