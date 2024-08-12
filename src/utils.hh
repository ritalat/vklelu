#pragma once

#include "memory.hh"

#include "glm/glm.hpp"
#include "vk_mem_alloc.h"
#include "vulkan/vulkan.h"

#include <memory>
#include <vector>

#define NS_IN_SEC 1000000000
#define US_IN_SEC 1000000
#define MS_IN_SEC 1000

#define VK_CHECK(x)                                                                                \
    do                                                                                             \
    {                                                                                              \
        VkResult err = x;                                                                          \
        if (err)                                                                                   \
        {                                                                                          \
            fprintf(stderr, "Detected Vulkan error %d at %s:%d.\n", int(err), __FILE__, __LINE__); \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

struct VertexInputDescription
{
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    VkPipelineVertexInputStateCreateFlags flags = 0;
};

struct Vertex
{
    bool operator==(const Vertex &other) const;
    static VertexInputDescription get_description();
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;
};

template <>
struct std::hash<Vertex> {
    size_t operator()(const Vertex &vertex) const;
};

struct Mesh
{
    void load_obj_file(const char *filename, const char *baseDir = nullptr);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::unique_ptr<BufferAllocation> vertexBuffer;
    std::unique_ptr<BufferAllocation> indexBuffer;
};

struct Material
{
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    VkDescriptorSet textureSet = VK_NULL_HANDLE;
};

struct Texture {
    std::unique_ptr<ImageAllocation> image;
    VkImageView imageView;
};

struct Himmeli
{
    Mesh *mesh;
    Material *material;
    glm::mat4 scale;
    glm::mat4 rotate;
    glm::mat4 translate;
};

struct PipelineBuilder
{
    void use_default_ff();
    VkPipeline build_pipeline(VkDevice device, VkRenderPass pass);
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly;
    VkViewport viewport;
    VkRect2D scissor;
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineColorBlendAttachmentState colorBlendAttachment;
    VkPipelineMultisampleStateCreateInfo multisampling;
    VkPipelineLayout pipelineLayout;
    VkPipelineDepthStencilStateCreateInfo depthStencil;
};
