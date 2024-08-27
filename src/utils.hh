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
Path get_asset_path(std::string_view file);
Path get_shader_path(std::string_view file);

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
