#include "utils.hh"

#include "struct_helpers.hh"

#include "vulkan/vulkan.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

const Path ASSET_DIR = [](){
    const char *env = getenv("VKLELU_ASSETDIR");
    if (env)
        return env;
    return "./assets";
}();

const Path SHADER_DIR = [](){
    const char *env = getenv("VKLELU_SHADERDIR");
    if (env)
        return env;
    return "./shaders";
}();

Path assetdir()
{
    return ASSET_DIR;
}

Path shaderdir()
{
    return SHADER_DIR;
}

Path get_asset_path(std::string_view file)
{
    return assetdir() / file;
}

Path get_shader_path(std::string_view file)
{
    return shaderdir() / file;
}

void PipelineBuilder::use_default_ff()
{
    inputAssembly = input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    rasterizer = rasterization_state_create_info(VK_POLYGON_MODE_FILL);
    multisampling = multisampling_state_create_info();
    colorBlendAttachment = color_blend_attachment_state();
    depthStencil = depth_stencil_create_info(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
}

VkPipeline PipelineBuilder::build_pipeline(VkDevice device, VkRenderPass pass)
{
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.pNext = nullptr;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.pNext = nullptr;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = nullptr;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.renderPass = pass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkPipeline newPipeline;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create pipeline\n");
        return VK_NULL_HANDLE;
    } else {
        return newPipeline;
    }
}
