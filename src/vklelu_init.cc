#include "vklelu.hh"

#include "context.hh"
#include "himmeli.hh"
#include "memory.hh"
#include "utils.hh"

#include "VkBootstrap.h"
#include "vulkan/vulkan.h"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

#define MAX_OBJECTS 10000

void VKlelu::initVulkan()
{
    initSwapchain();
    initCommands();
    initSyncStructures();
    initDescriptors();
    initPipelines();

    immediateSubmit([&](VkCommandBuffer cmd) {
        imageLayoutTransition(cmd, m_depthImage.image->image(),
                              VK_IMAGE_ASPECT_DEPTH_BIT,
                              VK_PIPELINE_STAGE_2_NONE,
                              0,
                              VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                              | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    });
}

void VKlelu::initSwapchain()
{
    vkb::SwapchainBuilder swapchainBuilder{ m_ctx->physicalDevice(), m_device, m_ctx->surface() };
    auto swapRet = swapchainBuilder.use_default_format_selection()
                                   .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                                   .set_desired_extent(m_fbSize.width, m_fbSize.height)
                                   .build();

    if (!swapRet)
        throw std::runtime_error("Failed to create swapchain. Error: " + swapRet.error().message());

    vkb::Swapchain vkbSwapchain = swapRet.value();
    m_swapchain = vkbSwapchain.swapchain;
    m_swapchainImageFormat = vkbSwapchain.image_format;

    m_swapchainData.resize(vkbSwapchain.image_count);
    std::vector<VkImage> swapchainImages = vkbSwapchain.get_images().value();
    std::vector<VkImageView> swapchainImageViews = vkbSwapchain.get_image_views().value();
    for (size_t i = 0; i < m_swapchainData.size(); ++i) {
        m_swapchainData[i].image = swapchainImages[i];
        m_swapchainData[i].imageView = swapchainImageViews[i];

        deferCleanup([=, this](){ vkDestroyImageView(m_device, m_swapchainData[i].imageView, nullptr); });
    }

    deferCleanup([=, this](){ vkDestroySwapchainKHR(m_device, m_swapchain, nullptr); });

    VkExtent3D imageExtent {
        .width = m_fbSize.width,
        .height = m_fbSize.height,
        .depth = 1
    };
    m_depthImageFormat = VK_FORMAT_D32_SFLOAT;

    m_depthImage.image = m_ctx->allocateImage(imageExtent, m_depthImageFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    m_depthImage.imageView = m_depthImage.image->createImageView(m_depthImageFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    fprintf(stderr, "Swapchain initialized\n");
}

void VKlelu::initCommands()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkCommandPoolCreateInfo commandPoolInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = m_ctx->graphicsQueueFamily()
        };

        VK_CHECK(vkCreateCommandPool(m_device, &commandPoolInfo, nullptr, &m_frameData[i].commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_frameData[i].commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };

        VK_CHECK(vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &m_frameData[i].mainCommandBuffer));

        deferCleanup([=, this](){ vkDestroyCommandPool(m_device, m_frameData[i].commandPool, nullptr); });
    }

    VkCommandPoolCreateInfo uploadPoolInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = m_ctx->graphicsQueueFamily()
    };

    VK_CHECK(vkCreateCommandPool(m_device, &uploadPoolInfo, nullptr, &m_uploadContext.commandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_uploadContext.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VK_CHECK(vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &m_uploadContext.commandBuffer));

    deferCleanup([=, this](){ vkDestroyCommandPool(m_device, m_uploadContext.commandPool, nullptr); });

    fprintf(stderr, "Command pool initialized\n");
}

void VKlelu::initSyncStructures()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkFenceCreateInfo fenceInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT
        };

        VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &m_frameData[i].renderFence));

        VkSemaphoreCreateInfo semaphoreInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };

        VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_frameData[i].imageAcquiredSemaphore));

        deferCleanup([=, this](){
            vkDestroyFence(m_device, m_frameData[i].renderFence, nullptr);
            vkDestroySemaphore(m_device, m_frameData[i].imageAcquiredSemaphore, nullptr);
        });
    }

    for (size_t i = 0; i < m_swapchainData.size(); ++i) {
        VkSemaphoreCreateInfo semaphoreInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };

        VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_swapchainData[i].renderSemaphore));

        deferCleanup([=, this](){
            vkDestroySemaphore(m_device, m_swapchainData[i].renderSemaphore, nullptr);
        });
    }

    VkFenceCreateInfo uploadFenceInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
    };

    VK_CHECK(vkCreateFence(m_device, &uploadFenceInfo, nullptr, &m_uploadContext.uploadFence));
    deferCleanup([=, this](){ vkDestroyFence(m_device, m_uploadContext.uploadFence, nullptr); });

    fprintf(stderr, "Sync structures initialized\n");
}

void VKlelu::initDescriptors()
{
    size_t sceneParamBufferSize = MAX_FRAMES_IN_FLIGHT * sizeof(SceneData);
    m_sceneParameterBuffer = m_ctx->allocateBuffer(sceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    m_sceneParameterBufferMapping = m_sceneParameterBuffer->map();

    VkDescriptorSetLayoutBinding camBind {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
    };

    VkDescriptorSetLayoutBinding sceneBind {
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
    };

    VkDescriptorSetLayoutBinding bindings[2] = { camBind, sceneBind };

    VkDescriptorSetLayoutCreateInfo setInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = bindings,
    };

    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &setInfo, nullptr, &m_globalSetLayout));

    deferCleanup([=, this](){ vkDestroyDescriptorSetLayout(m_device, m_globalSetLayout, nullptr); });

    VkDescriptorSetLayoutBinding objectBind {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
    };

    VkDescriptorSetLayoutCreateInfo set2Info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &objectBind
    };

    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &set2Info, nullptr, &m_objectSetLayout));

    deferCleanup([=, this](){ vkDestroyDescriptorSetLayout(m_device, m_objectSetLayout, nullptr); });

    VkDescriptorSetLayoutBinding textureBind {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };

    VkDescriptorSetLayoutBinding samplerBind {
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };

    VkDescriptorSetLayoutBinding set3Bind[] = { textureBind, samplerBind };

    VkDescriptorSetLayoutCreateInfo set3Info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = &set3Bind[0]
    };

    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &set3Info, nullptr, &m_singleTextureSetLayout));

    deferCleanup([=, this](){ vkDestroyDescriptorSetLayout(m_device, m_singleTextureSetLayout, nullptr); });

    std::vector<VkDescriptorPoolSize> sizes = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
                                                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10 },
                                                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 },
                                                { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 10 },
                                                { VK_DESCRIPTOR_TYPE_SAMPLER, 10 } };

    VkDescriptorPoolCreateInfo poolInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 10,
        .poolSizeCount = static_cast<uint32_t>(sizes.size()),
        .pPoolSizes = sizes.data()
    };

    VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool));

    deferCleanup([=, this](){ vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr); });

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_frameData[i].cameraBuffer = m_ctx->allocateBuffer(sizeof(CameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_frameData[i].cameraBufferMapping = m_frameData[i].cameraBuffer->map();
        m_frameData[i].objectBuffer = m_ctx->allocateBuffer(sizeof(ObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_frameData[i].objectBufferMapping = m_frameData[i].objectBuffer->map();

        VkDescriptorSetAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = m_descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &m_globalSetLayout
        };

        VK_CHECK(vkAllocateDescriptorSets(m_device, &allocInfo, &m_frameData[i].globalDescriptor));

        VkDescriptorSetAllocateInfo objAllocInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = m_descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &m_objectSetLayout
        };

        VK_CHECK(vkAllocateDescriptorSets(m_device, &objAllocInfo, &m_frameData[i].objectDescriptor));

        VkDescriptorBufferInfo camInfo {
            .buffer = m_frameData[i].cameraBuffer->buffer(),
            .offset = 0,
            .range = sizeof(CameraData)
        };

        VkDescriptorBufferInfo sceneInfo {
            .buffer = m_sceneParameterBuffer->buffer(),
            .offset = 0,
            .range = sizeof(SceneData)
        };

        VkDescriptorBufferInfo objInfo {
            .buffer = m_frameData[i].objectBuffer->buffer(),
            .offset = 0,
            .range = sizeof(ObjectData) * MAX_OBJECTS
        };

        VkWriteDescriptorSet camWrite {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_frameData[i].globalDescriptor,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &camInfo,
        };

        VkWriteDescriptorSet sceneWrite {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_frameData[i].globalDescriptor,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .pBufferInfo = &sceneInfo,
        };

        VkWriteDescriptorSet objWrite {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_frameData[i].objectDescriptor,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &objInfo,
        };

        VkWriteDescriptorSet writeSet[3] = { camWrite, sceneWrite, objWrite };
        vkUpdateDescriptorSets(m_device, 3, writeSet, 0 , nullptr);
    }

    fprintf(stderr, "Descriptors initialized\n");
}

void VKlelu::initPipelines()
{
    VkShaderModule fragShader;
    loadShader("shader.frag.spv", fragShader);
    fprintf(stderr, "Shader module shader.frag.spv created\n");

    VkShaderModule vertShader;
    loadShader("shader.vert.spv", vertShader);
    fprintf(stderr, "Shader module shader.vert.spv created\n");

    VkPushConstantRange pushConstant {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .size = sizeof(int)
    };

    VkDescriptorSetLayout setLayouts[3] = { m_globalSetLayout, m_objectSetLayout, m_singleTextureSetLayout };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 3,
        .pSetLayouts = &setLayouts[0],
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstant
    };

    VK_CHECK(vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_meshPipelineLayout));

    deferCleanup([=, this](){ vkDestroyPipelineLayout(m_device, m_meshPipelineLayout, nullptr); });

    VkPipelineShaderStageCreateInfo vertInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertShader,
        .pName = "main"
    };

    VkPipelineShaderStageCreateInfo fragInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragShader,
        .pName = "main"
    };

    VertexInputDescription vertexDescription = Vertex::getDescription();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = static_cast<uint32_t>(vertexDescription.bindings.size()),
        .pVertexBindingDescriptions = vertexDescription.bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexDescription.attributes.size()),
        .pVertexAttributeDescriptions = vertexDescription.attributes.data(),
    };

    PipelineBuilder builder;
    builder.useDefaultFF();
    builder.shaderStages.push_back(vertInfo);
    builder.shaderStages.push_back(fragInfo);
    builder.vertexInputInfo = vertexInputInfo;
    builder.viewport.x = 0.0f;
    builder.viewport.y = 0.0f;
    builder.viewport.width = static_cast<float>(m_fbSize.width);
    builder.viewport.height = static_cast<float>(m_fbSize.height);
    builder.viewport.minDepth = 0.0f;
    builder.viewport.maxDepth = 1.0f;
    builder.scissor.offset = { 0, 0 };
    builder.scissor.extent = m_fbSize;
    builder.pipelineLayout = m_meshPipelineLayout;
    m_meshPipeline = builder.buildPipeline(m_device, m_swapchainImageFormat, m_depthImageFormat);

    deferCleanup([=, this](){ vkDestroyPipeline(m_device, m_meshPipeline, nullptr); });

    vkDestroyShaderModule(m_device, vertShader, nullptr);
    vkDestroyShaderModule(m_device, fragShader, nullptr);

    if (!m_meshPipeline)
        throw std::runtime_error("Failed to create graphics pipeline \"mesh\"");

    fprintf(stderr, "Graphics pipelines initialized\n");
}
