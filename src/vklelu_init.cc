#include "vklelu.hh"

#include "context.hh"
#include "himmeli.hh"
#include "memory.hh"
#include "struct_helpers.hh"
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

        deferCleanup([=](){ vkDestroyImageView(m_device, m_swapchainData[i].imageView, nullptr); });
    }

    deferCleanup([=](){ vkDestroySwapchainKHR(m_device, m_swapchain, nullptr); });

    VkExtent3D imageExtent = {
        m_fbSize.width,
        m_fbSize.height,
        1
    };
    m_depthImageFormat = VK_FORMAT_D32_SFLOAT;

    m_depthImage.image = m_ctx->allocateImage(imageExtent, m_depthImageFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    m_depthImage.imageView = m_depthImage.image->createImageView(m_depthImageFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    fprintf(stderr, "Swapchain initialized\n");
}

void VKlelu::initCommands()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkCommandPoolCreateInfo commandPoolInfo = commandPoolCreateInfo(m_ctx->graphicsQueueFamily(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        VK_CHECK(vkCreateCommandPool(m_device, &commandPoolInfo, nullptr, &m_frameData[i].commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo = commandBufferAllocateInfo(m_frameData[i].commandPool, 1);
        VK_CHECK(vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &m_frameData[i].mainCommandBuffer));

        deferCleanup([=](){ vkDestroyCommandPool(m_device, m_frameData[i].commandPool, nullptr); });
    }

    VkCommandPoolCreateInfo uploadPoolInfo = commandPoolCreateInfo(m_ctx->graphicsQueueFamily());
    VK_CHECK(vkCreateCommandPool(m_device, &uploadPoolInfo, nullptr, &m_uploadContext.commandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = commandBufferAllocateInfo(m_uploadContext.commandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &m_uploadContext.commandBuffer));

    deferCleanup([=](){ vkDestroyCommandPool(m_device, m_uploadContext.commandPool, nullptr); });

    fprintf(stderr, "Command pool initialized\n");
}

void VKlelu::initSyncStructures()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkFenceCreateInfo fenceInfo = fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
        VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &m_frameData[i].renderFence));

        VkSemaphoreCreateInfo semaphoreInfo = semaphoreCreateInfo();
        VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_frameData[i].imageAcquiredSemaphore));

        deferCleanup([=](){
            vkDestroyFence(m_device, m_frameData[i].renderFence, nullptr);
            vkDestroySemaphore(m_device, m_frameData[i].imageAcquiredSemaphore, nullptr);
        });
    }

    for (size_t i = 0; i < m_swapchainData.size(); ++i) {
        VkSemaphoreCreateInfo semaphoreInfo = semaphoreCreateInfo();
        VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_swapchainData[i].renderSemaphore));

        deferCleanup([=](){
            vkDestroySemaphore(m_device, m_swapchainData[i].renderSemaphore, nullptr);
        });
    }

    VkFenceCreateInfo uploadFenceInfo = fenceCreateInfo();
    VK_CHECK(vkCreateFence(m_device, &uploadFenceInfo, nullptr, &m_uploadContext.uploadFence));
    deferCleanup([=](){ vkDestroyFence(m_device, m_uploadContext.uploadFence, nullptr); });

    fprintf(stderr, "Sync structures initialized\n");
}

void VKlelu::initDescriptors()
{
    size_t sceneParamBufferSize = MAX_FRAMES_IN_FLIGHT * sizeof(SceneData);
    m_sceneParameterBuffer = m_ctx->allocateBuffer(sceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    m_sceneParameterBufferMapping = m_sceneParameterBuffer->map();

    VkDescriptorSetLayoutBinding camBind = descriptorLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
    VkDescriptorSetLayoutBinding sceneBind = descriptorLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1);
    VkDescriptorSetLayoutBinding bindings[2] = { camBind, sceneBind };

    VkDescriptorSetLayoutCreateInfo setInfo = {};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.pNext = nullptr;
    setInfo.flags = 0;
    setInfo.bindingCount = 2;
    setInfo.pBindings = bindings;

    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &setInfo, nullptr, &m_globalSetLayout));

    deferCleanup([=](){ vkDestroyDescriptorSetLayout(m_device, m_globalSetLayout, nullptr); });

    VkDescriptorSetLayoutBinding objectBind = descriptorLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);

    VkDescriptorSetLayoutCreateInfo set2Info = {};
    set2Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set2Info.pNext = nullptr;
    set2Info.flags = 0;
    set2Info.bindingCount = 1;
    set2Info.pBindings = &objectBind;

    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &set2Info, nullptr, &m_objectSetLayout));

    deferCleanup([=](){ vkDestroyDescriptorSetLayout(m_device, m_objectSetLayout, nullptr); });

    VkDescriptorSetLayoutBinding textureBind = descriptorLayoutBinding(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
    VkDescriptorSetLayoutBinding samplerBind = descriptorLayoutBinding(VK_DESCRIPTOR_TYPE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1);
    VkDescriptorSetLayoutBinding set3Bind[] = { textureBind, samplerBind };

    VkDescriptorSetLayoutCreateInfo set3Info = {};
    set3Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set3Info.pNext = nullptr;
    set3Info.bindingCount = 2;
    set3Info.flags = 0;
    set3Info.pBindings = &set3Bind[0];

    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &set3Info, nullptr, &m_singleTextureSetLayout));

    deferCleanup([=](){ vkDestroyDescriptorSetLayout(m_device, m_singleTextureSetLayout, nullptr); });

    std::vector<VkDescriptorPoolSize> sizes = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
                                                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10 },
                                                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 },
                                                { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 10 },
                                                { VK_DESCRIPTOR_TYPE_SAMPLER, 10 } };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.pNext = nullptr;
    poolInfo.flags = 0;
    poolInfo.maxSets = 10;
    poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();

    VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool));

    deferCleanup([=](){ vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr); });

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_frameData[i].cameraBuffer = m_ctx->allocateBuffer(sizeof(CameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_frameData[i].cameraBufferMapping = m_frameData[i].cameraBuffer->map();
        m_frameData[i].objectBuffer = m_ctx->allocateBuffer(sizeof(ObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_frameData[i].objectBufferMapping = m_frameData[i].objectBuffer->map();

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.pNext = nullptr;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_globalSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(m_device, &allocInfo, &m_frameData[i].globalDescriptor));

        VkDescriptorSetAllocateInfo objAllocInfo = {};
        objAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        objAllocInfo.pNext = nullptr;
        objAllocInfo.descriptorPool = m_descriptorPool;
        objAllocInfo.descriptorSetCount = 1;
        objAllocInfo.pSetLayouts = &m_objectSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(m_device, &objAllocInfo, &m_frameData[i].objectDescriptor));

        VkDescriptorBufferInfo camInfo = {};
        camInfo.buffer = m_frameData[i].cameraBuffer->buffer();
        camInfo.offset = 0;
        camInfo.range = sizeof(CameraData);

        VkDescriptorBufferInfo sceneInfo = {};
        sceneInfo.buffer = m_sceneParameterBuffer->buffer();
        sceneInfo.offset = 0;
        sceneInfo.range = sizeof(SceneData);

        VkDescriptorBufferInfo objInfo = {};
        objInfo.buffer = m_frameData[i].objectBuffer->buffer();
        objInfo.offset = 0;
        objInfo.range = sizeof(ObjectData) * MAX_OBJECTS;

        VkWriteDescriptorSet camWrite = writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_frameData[i].globalDescriptor, &camInfo, 0);
        VkWriteDescriptorSet sceneWrite = writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, m_frameData[i].globalDescriptor, &sceneInfo, 1);
        VkWriteDescriptorSet objWrite = writeDescriptorBuffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_frameData[i].objectDescriptor, &objInfo, 0);
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

    VkPushConstantRange pushConstant;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(int);
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayout setLayouts[3] = { m_globalSetLayout, m_objectSetLayout, m_singleTextureSetLayout };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = pipelineLayoutCreateInfo();
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.setLayoutCount = 3;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    VK_CHECK(vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_meshPipelineLayout));

    deferCleanup([=](){ vkDestroyPipelineLayout(m_device, m_meshPipelineLayout, nullptr); });

    VertexInputDescription vertexDescription = Vertex::getDescription();

    PipelineBuilder builder;
    builder.useDefaultFF();
    builder.shaderStages.push_back(pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertShader));
    builder.shaderStages.push_back(pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragShader));
    builder.vertexInputInfo = vertexInputStateCreateInfo();
    builder.vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
    builder.vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexDescription.attributes.size());
    builder.vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
    builder.vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexDescription.bindings.size());
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

    deferCleanup([=](){ vkDestroyPipeline(m_device, m_meshPipeline, nullptr); });

    vkDestroyShaderModule(m_device, vertShader, nullptr);
    vkDestroyShaderModule(m_device, fragShader, nullptr);

    if (!m_meshPipeline)
        throw std::runtime_error("Failed to create graphics pipeline \"mesh\"");

    fprintf(stderr, "Graphics pipelines initialized\n");
}
