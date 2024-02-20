#include "vklelu.hh"

#include "context.hh"
#include "memory.hh"
#include "struct_helpers.hh"
#include "utils.hh"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "SDL.h"
#include "SDL_vulkan.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "vk_mem_alloc.h"
#include "VkBootstrap.h"
#include "vulkan/vulkan.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768

#define MAX_OBJECTS 10000

VKlelu::VKlelu(int argc, char *argv[]):
    frameCount(0),
    window(nullptr)
{
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error("Failed to init SDL");
    }

    window = SDL_CreateWindow("VKlelu",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              WINDOW_WIDTH, WINDOW_HEIGHT,
                              SDL_WINDOW_VULKAN);
    if (!window) {
        throw std::runtime_error("Failed to create SDL window");
    }

    int drawableWidth;
    int drawableHeight;
    SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
    fbSize.width = (uint32_t)drawableWidth;
    fbSize.height = (uint32_t)drawableHeight;

    fprintf(stderr, "Window size: %ux%u\n", WINDOW_WIDTH, WINDOW_HEIGHT);
    fprintf(stderr, "Drawable size: %ux%u\n", fbSize.width, fbSize.height);

    set_runtime_dirs();
}

VKlelu::~VKlelu()
{
    if (ctx)
        vkDeviceWaitIdle(ctx->device);

    for (auto destroyfn : resourceJanitor)
        destroyfn();

    if (window)
        SDL_DestroyWindow(window);

    SDL_Quit();
}

int VKlelu::run()
{
    if (!init_vulkan())
        return EXIT_FAILURE;

    init_scene();

    bool quit = false;
    SDL_Event event;

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    quit = true;
                    break;
                case SDL_KEYUP:
                    if (SDL_SCANCODE_ESCAPE == event.key.keysym.scancode)
                        quit = true;
                    break;
                default:
                    break;
            }
        }

        update();
        draw();
    }

    return EXIT_SUCCESS;
}

void VKlelu::draw()
{
    FrameData &currentFrame = get_current_frame();

    VK_CHECK(vkWaitForFences(ctx->device, 1, &currentFrame.renderFence, true, NS_IN_SEC));
    VK_CHECK(vkResetFences(ctx->device, 1, &currentFrame.renderFence));

    uint32_t swapchainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(ctx->device, swapchain, NS_IN_SEC, currentFrame.imageAcquiredSemaphore, nullptr, &swapchainImageIndex));

    VK_CHECK(vkResetCommandBuffer(currentFrame.mainCommandBuffer, 0));

    VkCommandBuffer cmd = currentFrame.mainCommandBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    VkClearValue clearValue;
    float flash = abs(sin(SDL_GetTicks() / 1000.0f));
    clearValue.color = { { 0.0f, 0.0f, flash, 1.0f } };

    VkClearValue depthClearValue;
    depthClearValue.depthStencil.depth = 1.0f;

    VkClearValue clearValues[2] = { clearValue, depthClearValue };

    VkRenderPassBeginInfo rpInfo = renderpass_begin_info(renderPass, fbSize, framebuffers[swapchainImageIndex]);
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = &clearValues[0];

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    draw_objects(cmd);

    vkCmdEndRenderPass(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit = submit_info(&cmd);
    submit.pWaitDstStageMask = &waitStage;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &currentFrame.imageAcquiredSemaphore;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &currentFrame.renderSemaphore;

    VK_CHECK(vkQueueSubmit(ctx->graphicsQueue, 1, &submit, currentFrame.renderFence));

    VkPresentInfoKHR presentInfo = present_info();
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &currentFrame.renderSemaphore;
    presentInfo.pImageIndices = &swapchainImageIndex;

    VK_CHECK(vkQueuePresentKHR(ctx->graphicsQueue, &presentInfo));

    ++frameCount;
}

void VKlelu::draw_objects(VkCommandBuffer cmd)
{
    FrameData &currentFrame = get_current_frame();

    glm::vec3 camera = { 0.0f, 0.0f, -5.0f };
    sceneParameters.cameraPos = glm::vec4{ camera, 1.0f };
    glm::mat4 view = glm::translate(glm::mat4(1.0f), camera);
    glm::mat4 projection = glm::perspective(glm::radians(70.0f), (float)fbSize.width/(float)fbSize.height, 0.1f, 200.0f);
    projection[1][1] *= -1;

    CameraData cam;
    cam.proj = projection;
    cam.view = view;
    cam.viewproj = projection * view;

    void *camData = currentFrame.cameraBufferMapping;
    memcpy(camData, &cam, sizeof(cam));

    char *sceneData = (char *)sceneParameterBufferMapping;
    int frameIndex = frameCount % MAX_FRAMES_IN_FLIGHT;
    sceneData += pad_uniform_buffer_size(sizeof(SceneData)) * frameIndex;
    memcpy(sceneData, &sceneParameters, sizeof(SceneData));

    void *objData = currentFrame.objectBufferMapping;;
    ObjectData *objectSSBO = (ObjectData *)objData;
    for (int i = 0; i < himmelit.size(); ++i) {
        objectSSBO[i].model = himmelit[i].translate * himmelit[i].rotate * himmelit[i].scale;
    }

    Mesh *lastMesh = nullptr;
    Material *lastMaterial = nullptr;

    for (int i = 0; i < himmelit.size(); ++i) {
        Himmeli &himmeli = himmelit[i];
        if (!himmeli.material || !himmeli.mesh)
            continue;

        if (himmeli.material != lastMaterial) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, himmeli.material->pipeline);
            lastMaterial = himmeli.material;
            uint32_t uniformOffset = pad_uniform_buffer_size(sizeof(SceneData)) * frameIndex;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, himmeli.material->pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 1, &uniformOffset);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, himmeli.material->pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);

            if (himmeli.material->textureSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, himmeli.material->pipelineLayout, 2, 1, &himmeli.material->textureSet, 0, nullptr);
            }
        }

        vkCmdPushConstants(cmd, himmeli.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(int), &i);

        if (himmeli.mesh != lastMesh) {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &himmeli.mesh->vertexBuffer->buffer, &offset);
            lastMesh = himmeli.mesh;
        }
        vkCmdDraw(cmd, himmeli.mesh->vertices.size(), 1, 0, 0);
    }
}

void VKlelu::update()
{
    for (Himmeli &himmeli : himmelit) {
        himmeli.rotate = glm::rotate(glm::mat4{ 1.0f }, glm::radians(SDL_GetTicks() / 20.0f), glm::vec3(0, 1, 0));
    }
}

FrameData &VKlelu::get_current_frame()
{
    return frameData[frameCount % MAX_FRAMES_IN_FLIGHT];
}

void VKlelu::set_runtime_dirs()
{
    char *ass = getenv("VKLELU_ASSETDIR");
    assetDir = ass ? ass : "./assets";
    assetDir.push_back('/');
    fprintf(stderr, "Asset directory: %s\n", assetDir.c_str());

    char *sdr = getenv("VKLELU_SHADERDIR");
    shaderDir = sdr ? sdr : "./shaders";
    shaderDir.push_back('/');
    fprintf(stderr, "Shader directory: %s\n", shaderDir.c_str());
}

void VKlelu::init_scene()
{
    load_meshes();
    load_images();

    create_material(meshPipeline, meshPipelineLayout, "monkey_material");

    Himmeli monkey;
    monkey.mesh = get_mesh("monkey");
    monkey.material = get_material("monkey_material");
    monkey.scale = glm::mat4{ 1.0f };
    monkey.rotate = glm::mat4{ 1.0f };
    monkey.translate = glm::mat4{ 1.0f };
    himmelit.push_back(monkey);

    Material *monkeyMat = get_material("monkey_material");

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.pNext = nullptr;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &singleTextureSetLayout;
    VK_CHECK(vkAllocateDescriptorSets(ctx->device, &allocInfo, &monkeyMat->textureSet));

    VkSamplerCreateInfo samplerInfo = sampler_create_info(VK_FILTER_LINEAR);
    VK_CHECK(vkCreateSampler(ctx->device, &samplerInfo, nullptr, &linearSampler));
    resourceJanitor.push_back([=](){ vkDestroySampler(ctx->device, linearSampler, nullptr); });

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.sampler = linearSampler;
    imageInfo.imageView = textures["monkey_diffuse"].imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet texture = write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, monkeyMat->textureSet, &imageInfo, 0);

    vkUpdateDescriptorSets(ctx->device, 1, &texture, 0, nullptr);

    sceneParameters.lightPos = { 10.0f, 10.0f, 10.0f, 0.0f };
    sceneParameters.lightColor = { 1.0f, 1.0f, 1.0f, 0.0f };
}

Material *VKlelu::create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string &name)
{
    Material mat;
    mat.pipeline = pipeline;
    mat.pipelineLayout = layout;
    materials[name] = mat;
    return &materials[name];
}

Mesh *VKlelu::get_mesh(const std::string &name)
{
    auto it = meshes.find(name);
    if (it == meshes.end())
        return nullptr;
    return &(*it).second;
}

Material *VKlelu::get_material(const std::string &name)
{
    auto it = materials.find(name);
    if (it == materials.end())
        return nullptr;
    return &(*it).second;
}

void VKlelu::load_meshes()
{
    Mesh monkeyMesh;
    monkeyMesh.load_obj_file("suzanne.obj", assetDir.c_str());
    upload_mesh(monkeyMesh);
    meshes["monkey"] = std::move(monkeyMesh);
}

void VKlelu::upload_mesh(Mesh &mesh)
{
    size_t bufferSize = mesh.vertices.size() * sizeof(Vertex);

    BufferAllocation stagingBuffer(ctx->allocator, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void *data = stagingBuffer.map();
    memcpy(data, mesh.vertices.data(), bufferSize);

    mesh.vertexBuffer = std::make_unique<BufferAllocation>(ctx->allocator, bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    immediate_submit([&](VkCommandBuffer cmd) {
        VkBufferCopy copy;
        copy.dstOffset = 0;
        copy.srcOffset = 0;
        copy.size = bufferSize;
        vkCmdCopyBuffer(cmd, stagingBuffer.buffer, mesh.vertexBuffer->buffer, 1, &copy);
    });
}

void VKlelu::load_images()
{
    Texture monkey;
    upload_image("suzanne_uv.png", monkey);
    monkey.imageView = monkey.image->create_image_view(ctx->device, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
    textures["monkey_diffuse"] = std::move(monkey);
}

void VKlelu::upload_image(const char *path, Texture &texture)
{
    int width;
    int height;
    int channels;

    std::string fullPath = assetDir + std::string(path);

    stbi_uc *pixels = stbi_load(fullPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        throw std::runtime_error("Failed to load image: " + std::string(path));
    }

    void *pixelPtr = pixels;
    VkDeviceSize imageSize = width * height * 4;
    VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;

    BufferAllocation stagingBuffer(ctx->allocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void *data = stagingBuffer.map();
    memcpy(data, pixelPtr, imageSize);

    stbi_image_free(pixels);

    VkExtent3D imageExtent;
    imageExtent.width = width;
    imageExtent.height = height;
    imageExtent.depth = 1;

    texture.image = std::make_unique<ImageAllocation>(ctx->allocator, imageExtent, imageFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    immediate_submit([&](VkCommandBuffer cmd) {
        VkImageSubresourceRange range;
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.pNext = nullptr;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.image = texture.image->image;
        barrier.subresourceRange = range;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy copyRegion = {};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = imageExtent;

        vkCmdCopyBufferToImage(cmd, stagingBuffer.buffer, texture.image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        VkImageMemoryBarrier barrier2 = barrier;
        barrier2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier2);
    });
}

void VKlelu::immediate_submit(std::function<void(VkCommandBuffer cmad)> &&function)
{
    VkCommandBuffer cmd = uploadContext.commandBuffer;
    VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo = submit_info(&cmd);

    VK_CHECK(vkQueueSubmit(ctx->graphicsQueue, 1, &submitInfo, uploadContext.uploadFence));

    vkWaitForFences(ctx->device, 1, &uploadContext.uploadFence, true, UINT64_MAX);
    vkResetFences(ctx->device, 1, &uploadContext.uploadFence);

    vkResetCommandPool(ctx->device, uploadContext.commandPool, 0);
}

void VKlelu::load_shader(const char *path, VkShaderModule &module)
{
    std::string fullPath = shaderDir + std::string(path);

    FILE *f = fopen(fullPath.c_str(), "rb");
    if (!f) {
        throw std::runtime_error("Failed to open file: " + std::string(path));
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<uint32_t> spv_data(fileSize / sizeof(uint32_t));
    size_t ret = fread(&spv_data[0], sizeof(spv_data[0]), spv_data.size(), f);
    fclose(f);

    if ((ret * sizeof(uint32_t)) != fileSize) {
        throw std::runtime_error("Failed to read file: " + std::string(path));
    }

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext = nullptr;
    createInfo.codeSize = spv_data.size() * sizeof(uint32_t);
    createInfo.pCode = &spv_data[0];

    if (vkCreateShaderModule(ctx->device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module: " + std::string(path));
    }
}

size_t VKlelu::pad_uniform_buffer_size(size_t originalSize)
{
    size_t minUboAllignment = ctx->physicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
    size_t alignedSize = originalSize;
    if (minUboAllignment > 0)
        alignedSize = (alignedSize + minUboAllignment - 1) & ~(minUboAllignment - 1);
    return alignedSize;
}

bool VKlelu::init_vulkan()
{
    ctx = std::make_unique<VulkanContext>(window);

    if (!init_swapchain())
        return false;

    if (!init_commands())
        return false;

    if (!init_default_renderpass())
        return false;

    if (!init_framebuffers())
        return false;

    if (!init_sync_structures())
        return false;

    if (!init_descriptors())
        return false;

    if (!init_pipelines())
        return false;

    return true;
}

bool VKlelu::init_swapchain()
{
    vkb::SwapchainBuilder swapchainBuilder{ ctx->physicalDevice, ctx->device, ctx->surface };
    auto swap_ret = swapchainBuilder.use_default_format_selection()
                                    .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                                    .set_desired_extent(fbSize.width, fbSize.height)
                                    .build();
    if (!swap_ret) {
        fprintf(stderr, "Failed to create swapchain. Error: %s\n", swap_ret.error().message().c_str());
        return false;
    }
    vkb::Swapchain vkb_swapchain = swap_ret.value();
    swapchain = vkb_swapchain.swapchain;
    swapchainImageFormat = vkb_swapchain.image_format;
    swapchainImages = vkb_swapchain.get_images().value();
    swapchainImageViews = vkb_swapchain.get_image_views().value();

    resourceJanitor.push_back([=](){ vkDestroySwapchainKHR(ctx->device, swapchain, nullptr); });

    VkExtent3D imageExtent = {
        fbSize.width,
        fbSize.height,
        1
    };
    depthImageFormat = VK_FORMAT_D32_SFLOAT;

    depthImage.image = std::make_unique<ImageAllocation>(ctx->allocator, imageExtent, depthImageFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    depthImage.imageView = depthImage.image->create_image_view(ctx->device, depthImageFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    fprintf(stderr, "Swapchain initialized successfully\n");
    return true;
}

bool VKlelu::init_commands()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkCommandPoolCreateInfo commandPoolInfo = command_pool_create_info(ctx->graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        VK_CHECK(vkCreateCommandPool(ctx->device, &commandPoolInfo, nullptr, &frameData[i].commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo = command_buffer_allocate_info(frameData[i].commandPool, 1);
        VK_CHECK(vkAllocateCommandBuffers(ctx->device, &cmdAllocInfo, &frameData[i].mainCommandBuffer));

        resourceJanitor.push_back([=](){ vkDestroyCommandPool(ctx->device, frameData[i].commandPool, nullptr); });
    }

    VkCommandPoolCreateInfo uploadPoolInfo = command_pool_create_info(ctx->graphicsQueueFamily);
    VK_CHECK(vkCreateCommandPool(ctx->device, &uploadPoolInfo, nullptr, &uploadContext.commandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = command_buffer_allocate_info(uploadContext.commandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(ctx->device, &cmdAllocInfo, &uploadContext.commandBuffer));

    resourceJanitor.push_back([=](){ vkDestroyCommandPool(ctx->device, uploadContext.commandPool, nullptr); });

    fprintf(stderr, "Command pool initialized successfully\n");
    return true;
}

bool VKlelu::init_default_renderpass()
{
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment = {};
    depthAttachment.flags = 0;
    depthAttachment.format = depthImageFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency depthDependency = {};
    depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    depthDependency.dstSubpass = 0;
    depthDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    depthDependency.srcAccessMask = 0;
    depthDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[2] = { colorAttachment, depthAttachment };
    VkSubpassDependency dependencies[2] = { dependency, depthDependency };

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = &attachments[0];
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = &dependencies[0];

    VK_CHECK(vkCreateRenderPass(ctx->device, &renderPassInfo, nullptr, &renderPass));

    resourceJanitor.push_back([=](){ vkDestroyRenderPass(ctx->device, renderPass, nullptr); });

    fprintf(stderr, "Renderpass initialized successfully\n");
    return true;
}

bool VKlelu::init_framebuffers()
{
    VkFramebufferCreateInfo fbInfo = framebuffer_create_info(renderPass, fbSize);

    uint32_t swapchainImageCount = swapchainImages.size();
    framebuffers = std::vector<VkFramebuffer>(swapchainImageCount);

    for (int i = 0; i < swapchainImageCount; ++i) {
        VkImageView attachments[2] = { swapchainImageViews[i], depthImage.imageView };
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = &attachments[0];
        VK_CHECK(vkCreateFramebuffer(ctx->device, &fbInfo, nullptr, &framebuffers[i]));

        resourceJanitor.push_back([=](){
            vkDestroyImageView(ctx->device, swapchainImageViews[i], nullptr);
            vkDestroyFramebuffer(ctx->device, framebuffers[i], nullptr);
        });
    }

    fprintf(stderr, "Framebuffers initialized successfully\n");
    return true;
}

bool VKlelu::init_sync_structures()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkFenceCreateInfo fenceCreateInfo = fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
        VK_CHECK(vkCreateFence(ctx->device, &fenceCreateInfo, nullptr, &frameData[i].renderFence));

        VkSemaphoreCreateInfo semaphoreCreateInfo = semaphore_create_info();
        VK_CHECK(vkCreateSemaphore(ctx->device, &semaphoreCreateInfo, nullptr, &frameData[i].imageAcquiredSemaphore));
        VK_CHECK(vkCreateSemaphore(ctx->device, &semaphoreCreateInfo, nullptr, &frameData[i].renderSemaphore));

        resourceJanitor.push_back([=](){
            vkDestroyFence(ctx->device, frameData[i].renderFence, nullptr);
            vkDestroySemaphore(ctx->device, frameData[i].imageAcquiredSemaphore, nullptr);
            vkDestroySemaphore(ctx->device, frameData[i].renderSemaphore, nullptr);
        });
    }

    VkFenceCreateInfo uploadFenceInfo = fence_create_info();
    VK_CHECK(vkCreateFence(ctx->device, &uploadFenceInfo, nullptr, &uploadContext.uploadFence));
    resourceJanitor.push_back([=](){ vkDestroyFence(ctx->device, uploadContext.uploadFence, nullptr); });

    fprintf(stderr, "Sync structures initialized successfully\n");
    return true;
}

bool VKlelu::init_descriptors()
{
    size_t sceneParamBufferSize = MAX_FRAMES_IN_FLIGHT * pad_uniform_buffer_size(sizeof(SceneData));
    sceneParameterBuffer = std::make_unique<BufferAllocation>(ctx->allocator, sceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
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

    VK_CHECK(vkCreateDescriptorSetLayout(ctx->device, &setInfo, nullptr, &globalSetLayout));

    resourceJanitor.push_back([=](){ vkDestroyDescriptorSetLayout(ctx->device, globalSetLayout, nullptr); });

    VkDescriptorSetLayoutBinding objectBind = descriptor_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);

    VkDescriptorSetLayoutCreateInfo set2Info = {};
    set2Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set2Info.pNext = nullptr;
    set2Info.flags = 0;
    set2Info.bindingCount = 1;
    set2Info.pBindings = &objectBind;

    VK_CHECK(vkCreateDescriptorSetLayout(ctx->device, &set2Info, nullptr, &objectSetLayout));

    resourceJanitor.push_back([=](){ vkDestroyDescriptorSetLayout(ctx->device, objectSetLayout, nullptr); });

    VkDescriptorSetLayoutBinding textureBind = descriptor_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);

    VkDescriptorSetLayoutCreateInfo set3Info = {};
    set3Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set3Info.pNext = nullptr;
    set3Info.bindingCount = 1;
    set3Info.flags = 0;
    set3Info.pBindings = &textureBind;

    VK_CHECK(vkCreateDescriptorSetLayout(ctx->device, &set3Info, nullptr, &singleTextureSetLayout));

    resourceJanitor.push_back([=](){ vkDestroyDescriptorSetLayout(ctx->device, singleTextureSetLayout, nullptr); });

    std::vector<VkDescriptorPoolSize> sizes = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
                                                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10 },
                                                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 },
                                                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 } };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.pNext = nullptr;
    poolInfo.flags = 0;
    poolInfo.maxSets = 10;
    poolInfo.poolSizeCount = sizes.size();
    poolInfo.pPoolSizes = sizes.data();

    VK_CHECK(vkCreateDescriptorPool(ctx->device, &poolInfo, nullptr, &descriptorPool));

    resourceJanitor.push_back([=](){ vkDestroyDescriptorPool(ctx->device, descriptorPool, nullptr); });

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        frameData[i].cameraBuffer = std::make_unique<BufferAllocation>(ctx->allocator, sizeof(CameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        frameData[i].cameraBufferMapping = frameData[i].cameraBuffer->map();
        frameData[i].objectBuffer = std::make_unique<BufferAllocation>(ctx->allocator, sizeof(ObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        frameData[i].objectBufferMapping = frameData[i].objectBuffer->map();

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.pNext = nullptr;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &globalSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(ctx->device, &allocInfo, &frameData[i].globalDescriptor));

        VkDescriptorSetAllocateInfo objAllocInfo = {};
        objAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        objAllocInfo.pNext = nullptr;
        objAllocInfo.descriptorPool = descriptorPool;
        objAllocInfo.descriptorSetCount = 1;
        objAllocInfo.pSetLayouts = &objectSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(ctx->device, &objAllocInfo, &frameData[i].objectDescriptor));

        VkDescriptorBufferInfo camInfo = {};
        camInfo.buffer = frameData[i].cameraBuffer->buffer;
        camInfo.offset = 0;
        camInfo.range = sizeof(CameraData);

        VkDescriptorBufferInfo sceneInfo = {};
        sceneInfo.buffer = sceneParameterBuffer->buffer;
        sceneInfo.offset = 0;
        sceneInfo.range = sizeof(SceneData);

        VkDescriptorBufferInfo objInfo = {};
        objInfo.buffer = frameData[i].objectBuffer->buffer;
        objInfo.offset = 0;
        objInfo.range = sizeof(ObjectData) * MAX_OBJECTS;

        VkWriteDescriptorSet camWrite = write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frameData[i].globalDescriptor, &camInfo, 0);
        VkWriteDescriptorSet sceneWrite = write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, frameData[i].globalDescriptor, &sceneInfo, 1);
        VkWriteDescriptorSet objWrite = write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frameData[i].objectDescriptor, &objInfo, 0);
        VkWriteDescriptorSet writeSet[3] = { camWrite, sceneWrite, objWrite };
        vkUpdateDescriptorSets(ctx->device, 3, writeSet, 0 , nullptr);
    }

    fprintf(stderr, "Descriptors initialized successfully\n");
    return true;
}

bool VKlelu::init_pipelines()
{
    VkShaderModule fragShader;
    load_shader("shader.frag.spv", fragShader);
    fprintf(stderr, "Fragment shader module shader.frag.spv created successfully\n");

    VkShaderModule vertShader;
    load_shader("shader.vert.spv", vertShader);
    fprintf(stderr, "Vertex shader module shader.vert.spv created successfully\n");

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
    VK_CHECK(vkCreatePipelineLayout(ctx->device, &pipelineLayoutInfo, nullptr, &meshPipelineLayout));

    resourceJanitor.push_back([=](){ vkDestroyPipelineLayout(ctx->device, meshPipelineLayout, nullptr); });

    VertexInputDescription vertexDescription = Vertex::get_description();

    PipelineBuilder builder;
    builder.use_default_ff();
    builder.shaderStages.push_back(pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertShader));
    builder.shaderStages.push_back(pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragShader));
    builder.vertexInputInfo = vertex_input_state_create_info();
    builder.vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
    builder.vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();
    builder.vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
    builder.vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();
    builder.viewport.x = 0.0f;
    builder.viewport.y = 0.0f;
    builder.viewport.width = fbSize.width;
    builder.viewport.height = fbSize.height;
    builder.viewport.minDepth = 0.0f;
    builder.viewport.maxDepth = 1.0f;
    builder.scissor.offset = { 0, 0 };
    builder.scissor.extent = fbSize;
    builder.pipelineLayout = meshPipelineLayout;
    meshPipeline = builder.build_pipeline(ctx->device, renderPass);

    resourceJanitor.push_back([=](){ vkDestroyPipeline(ctx->device, meshPipeline, nullptr); });

    vkDestroyShaderModule(ctx->device, vertShader, nullptr);
    vkDestroyShaderModule(ctx->device, fragShader, nullptr);

    if (!meshPipeline) {
        fprintf(stderr, "Failed to create graphics pipeline \"mesh\"\n");
        return false;
    }

    fprintf(stderr, "Graphics pipelines initialized successfully\n");
    return true;
}
