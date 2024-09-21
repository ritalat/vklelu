#include "vklelu.hh"

#include "context.hh"
#include "himmeli.hh"
#include "memory.hh"
#include "struct_helpers.hh"
#include "utils.hh"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "SDL.h"
#include "SDL_vulkan.h"
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

VKlelu::VKlelu(int argc, char *argv[]):
    frameCount(0),
    window(nullptr)
{
    (void)argc;
    (void)argv;

    fprintf(stderr, "Launching VKlelu\n"
                    "================\n");

    SDL_version compiled;
    SDL_version linked;
    SDL_VERSION(&compiled);
    SDL_GetVersion(&linked);
    fprintf(stderr, "Compiled with:\tSDL %u.%u.%u\n",
            compiled.major, compiled.minor, compiled.patch);
    fprintf(stderr, "Loaded:\t\tSDL %u.%u.%u\n",
            linked.major, linked.minor, linked.patch);

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

    fprintf(stderr, "Window size:\t%ux%u\n", WINDOW_WIDTH, WINDOW_HEIGHT);
    fprintf(stderr, "Drawable size:\t%ux%u\n", fbSize.width, fbSize.height);

    fprintf(stderr, "Asset directory:\t%s\n", cpath(assetdir()));
    fprintf(stderr, "Shader directory:\t%s\n", cpath(shaderdir()));
}

VKlelu::~VKlelu()
{
    if (ctx)
        vkDeviceWaitIdle(ctx->device);

    for (auto fn = resourceJanitor.rbegin(); fn != resourceJanitor.rend(); ++fn)
        (*fn)();

    if (window)
        SDL_DestroyWindow(window);

    SDL_Quit();
}

int VKlelu::run()
{
    init_vulkan();
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

void VKlelu::update()
{
    for (Himmeli &himmeli : himmelit) {
        himmeli.rotate = glm::rotate(glm::mat4{ 1.0f }, glm::radians(static_cast<float>(SDL_GetTicks()) / 20.0f), glm::vec3(0, 1, 0));
    }
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

    image_layout_transition(cmd, swapchainImages[swapchainImageIndex],
                                 VK_IMAGE_ASPECT_COLOR_BIT,
                                 VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                                 0,
                                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    image_layout_transition(cmd, depthImage.image->image,
                                 VK_IMAGE_ASPECT_DEPTH_BIT,
                                 VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                                 0,
                                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                 | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingAttachmentInfo colorInfo = {};
    colorInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorInfo.imageView = swapchainImageViews[swapchainImageIndex];
    colorInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorInfo.clearValue.color = { { 0.0f, 0.0f, 0.5f, 1.0f } };

    VkRenderingAttachmentInfo depthInfo = {};
    depthInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthInfo.imageView = depthImage.imageView;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthInfo.clearValue.depthStencil.depth = 1.0f;

    VkRenderingInfo renderInfo = {};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea.extent = fbSize;
    renderInfo.renderArea.offset = { 0, 0 };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorInfo;
    renderInfo.pDepthAttachment = &depthInfo;

    vkCmdBeginRendering(cmd, &renderInfo);

    draw_objects(cmd);

    vkCmdEndRendering(cmd);

    image_layout_transition(cmd, swapchainImages[swapchainImageIndex],
                                 VK_IMAGE_ASPECT_COLOR_BIT,
                                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                                 0,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

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
    glm::mat4 projection = glm::perspective(glm::radians(70.0f), static_cast<float>(fbSize.width)/static_cast<float>(fbSize.height), 0.1f, 200.0f);
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
    for (size_t i = 0; i < himmelit.size(); ++i) {
        objectSSBO[i].model = himmelit[i].translate * himmelit[i].rotate * himmelit[i].scale;
    }

    Mesh *lastMesh = nullptr;
    Material *lastMaterial = nullptr;

    for (size_t i = 0; i < himmelit.size(); ++i) {
        Himmeli &himmeli = himmelit[i];
        if (!himmeli.material || !himmeli.mesh)
            continue;

        if (himmeli.material != lastMaterial) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, himmeli.material->pipeline);
            lastMaterial = himmeli.material;
            uint32_t uniformOffset = static_cast<uint32_t>(pad_uniform_buffer_size(sizeof(SceneData))) * frameIndex;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, himmeli.material->pipelineLayout, 0, 1, &currentFrame.globalDescriptor, 1, &uniformOffset);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, himmeli.material->pipelineLayout, 1, 1, &currentFrame.objectDescriptor, 0, nullptr);

            if (himmeli.material->textureSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, himmeli.material->pipelineLayout, 2, 1, &himmeli.material->textureSet, 0, nullptr);
            }
        }

        int ii = static_cast<int>(i);
        vkCmdPushConstants(cmd, himmeli.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(int), &ii);

        if (himmeli.mesh != lastMesh) {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &himmeli.mesh->vertexBuffer->buffer, &offset);
            vkCmdBindIndexBuffer(cmd, himmeli.mesh->indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
            lastMesh = himmeli.mesh;
        }
        vkCmdDrawIndexed(cmd, himmeli.mesh->numIndices, 1, 0, 0, 0);
    }
}

FrameData &VKlelu::get_current_frame()
{
    return frameData[frameCount % MAX_FRAMES_IN_FLIGHT];
}

void VKlelu::init_scene()
{
    ObjFile monkeyObj("suzanne.obj");
    upload_mesh(monkeyObj, "monkey");

    ImageFile monkeyTexture("suzanne_uv.png");
    upload_image(monkeyTexture, "monkey_diffuse");

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

Material *VKlelu::create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string name)
{
    Material mat;
    mat.pipeline = pipeline;
    mat.pipelineLayout = layout;
    materials[name] = mat;
    return &materials[name];
}

Mesh *VKlelu::get_mesh(const std::string name)
{
    auto it = meshes.find(name);
    if (it == meshes.end())
        return nullptr;
    return &(*it).second;
}

Material *VKlelu::get_material(const std::string name)
{
    auto it = materials.find(name);
    if (it == materials.end())
        return nullptr;
    return &(*it).second;
}

void VKlelu::upload_mesh(ObjFile &obj, std::string name)
{
    Mesh mesh;
    mesh.numVertices = static_cast<uint32_t>(obj.vertices.size());
    mesh.numIndices = static_cast<uint32_t>(obj.indices.size());
    size_t vertexBufferSize = mesh.numVertices * sizeof(Vertex);
    size_t indexBufferSize = mesh.numIndices * sizeof(uint32_t);

    BufferAllocation stagingBuffer(ctx->allocator, vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void *data = stagingBuffer.map();
    memcpy(data, obj.vertices.data(), vertexBufferSize);
    memcpy((uint8_t *)data + vertexBufferSize, obj.indices.data(), indexBufferSize);

    mesh.vertexBuffer = std::make_unique<BufferAllocation>(ctx->allocator, vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    mesh.indexBuffer = std::make_unique<BufferAllocation>(ctx->allocator, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    immediate_submit([&](VkCommandBuffer cmd) {
        VkBufferCopy copy;
        copy.dstOffset = 0;
        copy.srcOffset = 0;
        copy.size = vertexBufferSize;
        vkCmdCopyBuffer(cmd, stagingBuffer.buffer, mesh.vertexBuffer->buffer, 1, &copy);
        copy.srcOffset = vertexBufferSize;
        copy.size = indexBufferSize;
        vkCmdCopyBuffer(cmd, stagingBuffer.buffer, mesh.indexBuffer->buffer, 1, &copy);
    });

    meshes[name] = std::move(mesh);
}

void VKlelu::upload_image(ImageFile &image, std::string name)
{
    Texture texture;
    VkDeviceSize imageSize = image.width * image.height * 4;
    VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;

    BufferAllocation stagingBuffer(ctx->allocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void *data = stagingBuffer.map();
    memcpy(data, image.pixels, imageSize);

    VkExtent3D imageExtent;
    imageExtent.width = image.width;
    imageExtent.height = image.height;
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

    texture.imageView = texture.image->create_image_view(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
    textures[name] = std::move(texture);
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
    Path fullPath = get_shader_path(path);

    FILE *f = fopen(cpath(fullPath), "rb");
    if (!f) {
        throw std::runtime_error("Failed to open file: " + std::string(path));
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<uint32_t> spv_data(fileSize / sizeof(uint32_t));
    size_t ret = fread(&spv_data[0], sizeof(spv_data[0]), spv_data.size(), f);
    fclose(f);

    if ((ret * sizeof(uint32_t)) != static_cast<size_t>(fileSize)) {
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
