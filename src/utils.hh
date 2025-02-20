#pragma once

#include "vulkan/vulkan.h"

#include <cstdio>
#include <filesystem>
#include <string_view>
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

using Path = std::filesystem::path;

#ifdef WIN32
// Windows std path is internally wchar :/
#define cpath(path) path.string().data()
#else
#define cpath(path) path.c_str()
#endif

Path assetdir();
Path shaderdir();
Path getAssetPath(std::string_view file);
Path getShaderPath(std::string_view file);

void imageLayoutTransition(VkCommandBuffer cmd,
                           VkImage image,
                           VkImageAspectFlags aspectFlags,
                           VkPipelineStageFlags2 srcStageFlags,
                           VkAccessFlags srcAccessFlags,
                           VkPipelineStageFlags2 dstStageFlags,
                           VkAccessFlags dstAccessFlags,
                           VkImageLayout oldLayout,
                           VkImageLayout newLayout);

struct PipelineBuilder
{
    void useDefaultFF();
    VkPipeline buildPipeline(VkDevice device, VkFormat colorFormat, VkFormat depthFormat);
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
