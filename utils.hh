#pragma once

#include "glm/glm.hpp"
#include "vk_mem_alloc.h"
#include "vulkan/vulkan.h"

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

struct ImageAllocation
{
    VkImage image;
    VmaAllocation allocation;
};

struct BufferAllocation
{
    VkBuffer buffer;
    VmaAllocation allocation;
};

struct VertexInputDescription
{
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    VkPipelineVertexInputStateCreateFlags flags = 0;
};

struct Vertex
{
    static VertexInputDescription get_description();
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

struct Mesh
{
    bool load_obj_file(const char *filename, const char *baseDir = nullptr);
    std::vector<Vertex> vertices;
    BufferAllocation vertexBuffer;
};

VkImageCreateInfo image_create_info(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);
VkImageViewCreateInfo imageview_create_info(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);
VkCommandPoolCreateInfo command_pool_create_info(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags = 0);
VkCommandBufferAllocateInfo command_buffer_allocate_info(VkCommandPool pool, uint32_t count = 1, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
VkCommandBufferBeginInfo command_buffer_begin_info(VkCommandBufferUsageFlags flags = 0);
VkFramebufferCreateInfo framebuffer_create_info(VkRenderPass renderPass, VkExtent2D extent);
VkFenceCreateInfo fence_create_info(VkFenceCreateFlags flags = 0);
VkSemaphoreCreateInfo semaphore_create_info(VkSemaphoreCreateFlags flags = 0);
VkRenderPassBeginInfo renderpass_begin_info(VkRenderPass renderPass, VkExtent2D windowExtent, VkFramebuffer framebuffer);
VkSubmitInfo submit_info(VkCommandBuffer* cmd);
VkPresentInfoKHR present_info();
VkPipelineShaderStageCreateInfo pipeline_shader_stage_create_info(VkShaderStageFlagBits stage, VkShaderModule shaderModule);
VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info();
VkPipelineInputAssemblyStateCreateInfo input_assembly_create_info(VkPrimitiveTopology topology);
VkPipelineRasterizationStateCreateInfo rasterization_state_create_info(VkPolygonMode polygonMode);
VkPipelineMultisampleStateCreateInfo multisampling_state_create_info();
VkPipelineColorBlendAttachmentState color_blend_attachment_state();
VkPipelineLayoutCreateInfo pipeline_layout_create_info();
VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info(VkBool32 depthTest, VkBool32 depthWrite, VkCompareOp compareOp = VK_COMPARE_OP_ALWAYS);

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
