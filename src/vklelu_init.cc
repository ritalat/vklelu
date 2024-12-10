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

void VKlelu::init_vulkan()
{
    ctx = std::make_unique<VulkanContext>(window);
    init_swapchain();
    init_commands();
    init_sync_structures();
    init_descriptors();
    init_pipelines();
}

void VKlelu::init_swapchain()
{
    vkb::SwapchainBuilder swapchainBuilder{ ctx->physical_device(), ctx->device(), ctx->surface() };
    auto swap_ret = swapchainBuilder.use_default_format_selection()
                                    .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                                    .set_desired_extent(fbSize.width, fbSize.height)
                                    .build();

    if (!swap_ret)
        throw std::runtime_error("Failed to create swapchain. Error: " + swap_ret.error().message());

    vkb::Swapchain vkb_swapchain = swap_ret.value();
    swapchain = vkb_swapchain.swapchain;
    swapchainImageFormat = vkb_swapchain.image_format;
    swapchainImages = vkb_swapchain.get_images().value();
    swapchainImageViews = vkb_swapchain.get_image_views().value();

    resourceJanitor.push_back([=](){ vkDestroySwapchainKHR(ctx->device(), swapchain, nullptr); });
    for (VkImageView swapView : swapchainImageViews) {
        resourceJanitor.push_back([=](){ vkDestroyImageView(ctx->device(), swapView, nullptr); });
    }

    VkExtent3D imageExtent = {
        fbSize.width,
        fbSize.height,
        1
    };
    depthImageFormat = VK_FORMAT_D32_SFLOAT;

    depthImage.image = ctx->allocate_image(imageExtent, depthImageFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    depthImage.imageView = depthImage.image->create_image_view(depthImageFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    fprintf(stderr, "Swapchain initialized\n");
}

void VKlelu::init_commands()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkCommandPoolCreateInfo commandPoolInfo = command_pool_create_info(ctx->graphics_queue_family(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        VK_CHECK(vkCreateCommandPool(ctx->device(), &commandPoolInfo, nullptr, &frameData[i].commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo = command_buffer_allocate_info(frameData[i].commandPool, 1);
        VK_CHECK(vkAllocateCommandBuffers(ctx->device(), &cmdAllocInfo, &frameData[i].mainCommandBuffer));

        resourceJanitor.push_back([=](){ vkDestroyCommandPool(ctx->device(), frameData[i].commandPool, nullptr); });
    }

    VkCommandPoolCreateInfo uploadPoolInfo = command_pool_create_info(ctx->graphics_queue_family());
    VK_CHECK(vkCreateCommandPool(ctx->device(), &uploadPoolInfo, nullptr, &uploadContext.commandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = command_buffer_allocate_info(uploadContext.commandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(ctx->device(), &cmdAllocInfo, &uploadContext.commandBuffer));

    resourceJanitor.push_back([=](){ vkDestroyCommandPool(ctx->device(), uploadContext.commandPool, nullptr); });

    fprintf(stderr, "Command pool initialized\n");
}

void VKlelu::init_sync_structures()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkFenceCreateInfo fenceCreateInfo = fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
        VK_CHECK(vkCreateFence(ctx->device(), &fenceCreateInfo, nullptr, &frameData[i].renderFence));

        VkSemaphoreCreateInfo semaphoreCreateInfo = semaphore_create_info();
        VK_CHECK(vkCreateSemaphore(ctx->device(), &semaphoreCreateInfo, nullptr, &frameData[i].imageAcquiredSemaphore));
        VK_CHECK(vkCreateSemaphore(ctx->device(), &semaphoreCreateInfo, nullptr, &frameData[i].renderSemaphore));

        resourceJanitor.push_back([=](){
            vkDestroyFence(ctx->device(), frameData[i].renderFence, nullptr);
            vkDestroySemaphore(ctx->device(), frameData[i].imageAcquiredSemaphore, nullptr);
            vkDestroySemaphore(ctx->device(), frameData[i].renderSemaphore, nullptr);
        });
    }

    VkFenceCreateInfo uploadFenceInfo = fence_create_info();
    VK_CHECK(vkCreateFence(ctx->device(), &uploadFenceInfo, nullptr, &uploadContext.uploadFence));
    resourceJanitor.push_back([=](){ vkDestroyFence(ctx->device(), uploadContext.uploadFence, nullptr); });

    fprintf(stderr, "Sync structures initialized\n");
}

void VKlelu::init_descriptors()
{
    size_t sceneParamBufferSize = MAX_FRAMES_IN_FLIGHT * sizeof(SceneData);
    sceneParameterBuffer = ctx->allocate_buffer(sceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    sceneParameterBufferMapping = sceneParameterBuffer->map();

    VkDescriptorSetLayoutBinding camBind = descriptor_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
    VkDescriptorSetLayoutBinding sceneBind = descriptor_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1);
    VkDescriptorSetLayoutBinding bindings[2] = { camBind, sceneBind };

    VkDescriptorSetLayoutCreateInfo setInfo = {};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.pNext = nullptr;
    setInfo.flags = 0;
    setInfo.bindingCount = 2;
    setInfo.pBindings = bindings;

    VK_CHECK(vkCreateDescriptorSetLayout(ctx->device(), &setInfo, nullptr, &globalSetLayout));

    resourceJanitor.push_back([=](){ vkDestroyDescriptorSetLayout(ctx->device(), globalSetLayout, nullptr); });

    VkDescriptorSetLayoutBinding objectBind = descriptor_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);

    VkDescriptorSetLayoutCreateInfo set2Info = {};
    set2Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set2Info.pNext = nullptr;
    set2Info.flags = 0;
    set2Info.bindingCount = 1;
    set2Info.pBindings = &objectBind;

    VK_CHECK(vkCreateDescriptorSetLayout(ctx->device(), &set2Info, nullptr, &objectSetLayout));

    resourceJanitor.push_back([=](){ vkDestroyDescriptorSetLayout(ctx->device(), objectSetLayout, nullptr); });

    VkDescriptorSetLayoutBinding textureBind = descriptor_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);

    VkDescriptorSetLayoutCreateInfo set3Info = {};
    set3Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set3Info.pNext = nullptr;
    set3Info.bindingCount = 1;
    set3Info.flags = 0;
    set3Info.pBindings = &textureBind;

    VK_CHECK(vkCreateDescriptorSetLayout(ctx->device(), &set3Info, nullptr, &singleTextureSetLayout));

    resourceJanitor.push_back([=](){ vkDestroyDescriptorSetLayout(ctx->device(), singleTextureSetLayout, nullptr); });

    std::vector<VkDescriptorPoolSize> sizes = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
                                                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10 },
                                                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 },
                                                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 } };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.pNext = nullptr;
    poolInfo.flags = 0;
    poolInfo.maxSets = 10;
    poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();

    VK_CHECK(vkCreateDescriptorPool(ctx->device(), &poolInfo, nullptr, &descriptorPool));

    resourceJanitor.push_back([=](){ vkDestroyDescriptorPool(ctx->device(), descriptorPool, nullptr); });

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        frameData[i].cameraBuffer = ctx->allocate_buffer(sizeof(CameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        frameData[i].cameraBufferMapping = frameData[i].cameraBuffer->map();
        frameData[i].objectBuffer = ctx->allocate_buffer(sizeof(ObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        frameData[i].objectBufferMapping = frameData[i].objectBuffer->map();

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.pNext = nullptr;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &globalSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(ctx->device(), &allocInfo, &frameData[i].globalDescriptor));

        VkDescriptorSetAllocateInfo objAllocInfo = {};
        objAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        objAllocInfo.pNext = nullptr;
        objAllocInfo.descriptorPool = descriptorPool;
        objAllocInfo.descriptorSetCount = 1;
        objAllocInfo.pSetLayouts = &objectSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(ctx->device(), &objAllocInfo, &frameData[i].objectDescriptor));

        VkDescriptorBufferInfo camInfo = {};
        camInfo.buffer = frameData[i].cameraBuffer->buffer();
        camInfo.offset = 0;
        camInfo.range = sizeof(CameraData);

        VkDescriptorBufferInfo sceneInfo = {};
        sceneInfo.buffer = sceneParameterBuffer->buffer();
        sceneInfo.offset = 0;
        sceneInfo.range = sizeof(SceneData);

        VkDescriptorBufferInfo objInfo = {};
        objInfo.buffer = frameData[i].objectBuffer->buffer();
        objInfo.offset = 0;
        objInfo.range = sizeof(ObjectData) * MAX_OBJECTS;

        VkWriteDescriptorSet camWrite = write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frameData[i].globalDescriptor, &camInfo, 0);
        VkWriteDescriptorSet sceneWrite = write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, frameData[i].globalDescriptor, &sceneInfo, 1);
        VkWriteDescriptorSet objWrite = write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frameData[i].objectDescriptor, &objInfo, 0);
        VkWriteDescriptorSet writeSet[3] = { camWrite, sceneWrite, objWrite };
        vkUpdateDescriptorSets(ctx->device(), 3, writeSet, 0 , nullptr);
    }

    fprintf(stderr, "Descriptors initialized\n");
}

void VKlelu::init_pipelines()
{
    VkShaderModule fragShader;
    load_shader("shader.frag.spv", fragShader);
    fprintf(stderr, "Shader module shader.frag.spv created\n");

    VkShaderModule vertShader;
    load_shader("shader.vert.spv", vertShader);
    fprintf(stderr, "Shader module shader.vert.spv created\n");

    VkPushConstantRange pushConstant;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(int);
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayout setLayouts[3] = { globalSetLayout, objectSetLayout, singleTextureSetLayout };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = pipeline_layout_create_info();
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.setLayoutCount = 3;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    VK_CHECK(vkCreatePipelineLayout(ctx->device(), &pipelineLayoutInfo, nullptr, &meshPipelineLayout));

    resourceJanitor.push_back([=](){ vkDestroyPipelineLayout(ctx->device(), meshPipelineLayout, nullptr); });

    VertexInputDescription vertexDescription = Vertex::get_description();

    PipelineBuilder builder;
    builder.use_default_ff();
    builder.shaderStages.push_back(pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertShader));
    builder.shaderStages.push_back(pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragShader));
    builder.vertexInputInfo = vertex_input_state_create_info();
    builder.vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
    builder.vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexDescription.attributes.size());
    builder.vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
    builder.vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexDescription.bindings.size());
    builder.viewport.x = 0.0f;
    builder.viewport.y = 0.0f;
    builder.viewport.width = static_cast<float>(fbSize.width);
    builder.viewport.height = static_cast<float>(fbSize.height);
    builder.viewport.minDepth = 0.0f;
    builder.viewport.maxDepth = 1.0f;
    builder.scissor.offset = { 0, 0 };
    builder.scissor.extent = fbSize;
    builder.pipelineLayout = meshPipelineLayout;
    meshPipeline = builder.build_pipeline(ctx->device(), swapchainImageFormat, depthImageFormat);

    resourceJanitor.push_back([=](){ vkDestroyPipeline(ctx->device(), meshPipeline, nullptr); });

    vkDestroyShaderModule(ctx->device(), vertShader, nullptr);
    vkDestroyShaderModule(ctx->device(), fragShader, nullptr);

    if (!meshPipeline)
        throw std::runtime_error("Failed to create graphics pipeline \"mesh\"");

    fprintf(stderr, "Graphics pipelines initialized\n");
}
