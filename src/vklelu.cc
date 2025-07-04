#include "vklelu.hh"

#include "context.hh"
#include "himmeli.hh"
#include "memory.hh"
#include "struct_helpers.hh"
#include "utils.hh"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"
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
    m_frameCount(0)
{
    (void)argc;
    (void)argv;

    fprintf(stderr, "Launching VKlelu\n"
                    "================\n");

    int linked = SDL_GetVersion();
    fprintf(stderr, "Compiled with:\tSDL %u.%u.%u\n",
            SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
    fprintf(stderr, "Loaded:\t\tSDL %u.%u.%u\n",
            SDL_VERSIONNUM_MAJOR(linked),
            SDL_VERSIONNUM_MINOR(linked),
            SDL_VERSIONNUM_MICRO(linked));

    m_ctx = std::make_unique<VulkanContext>(WINDOW_WIDTH, WINDOW_HEIGHT);
    m_window = m_ctx->window();
    m_device = m_ctx->device();

    int drawableWidth;
    int drawableHeight;
    SDL_GetWindowSizeInPixels(m_window, &drawableWidth, &drawableHeight);
    m_fbSize.width = (uint32_t)drawableWidth;
    m_fbSize.height = (uint32_t)drawableHeight;

    fprintf(stderr, "Window size:\t%ux%u\n", WINDOW_WIDTH, WINDOW_HEIGHT);
    fprintf(stderr, "Drawable size:\t%ux%u\n", m_fbSize.width, m_fbSize.height);

    fprintf(stderr, "Asset directory:\t%s\n", cpath(assetdir()));
    fprintf(stderr, "Shader directory:\t%s\n", cpath(shaderdir()));
}

VKlelu::~VKlelu()
{
    if (m_ctx)
        vkDeviceWaitIdle(m_device);

    for (auto fn = m_resourceJanitor.rbegin(); fn != m_resourceJanitor.rend(); ++fn)
        (*fn)();
}

int VKlelu::run()
{
    initVulkan();
    initScene();

    bool quit = false;
    SDL_Event event;

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    quit = true;
                    break;
                case SDL_EVENT_KEY_UP:
                    if (SDL_SCANCODE_ESCAPE == event.key.scancode)
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
    for (Himmeli &himmeli : m_himmelit) {
        himmeli.rotate = glm::rotate(glm::mat4{ 1.0f }, glm::radians(static_cast<float>(SDL_GetTicks()) / 20.0f), glm::vec3(0, 1, 0));
    }
}

void VKlelu::draw()
{
    FrameData &currentFrame = getCurrentFrame();

    VK_CHECK(vkWaitForFences(m_device, 1, &currentFrame.renderFence, true, NS_IN_SEC));
    VK_CHECK(vkResetFences(m_device, 1, &currentFrame.renderFence));

    uint32_t swapchainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(m_device, m_swapchain, NS_IN_SEC, currentFrame.imageAcquiredSemaphore, nullptr, &swapchainImageIndex));

    SwapchainData &currentImage = m_swapchainData[swapchainImageIndex];

    VK_CHECK(vkResetCommandBuffer(currentFrame.mainCommandBuffer, 0));

    VkCommandBuffer cmd = currentFrame.mainCommandBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo = commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    imageLayoutTransition(cmd, currentImage.image,
                               VK_IMAGE_ASPECT_COLOR_BIT,
                               VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                               0,
                               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    imageLayoutTransition(cmd, m_depthImage.image->image(),
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
    colorInfo.imageView = currentImage.imageView;
    colorInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorInfo.clearValue.color = { { 0.0f, 0.0f, 0.5f, 1.0f } };

    VkRenderingAttachmentInfo depthInfo = {};
    depthInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthInfo.imageView = m_depthImage.imageView;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthInfo.clearValue.depthStencil.depth = 1.0f;

    VkRenderingInfo renderInfo = {};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea.extent = m_fbSize;
    renderInfo.renderArea.offset = { 0, 0 };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorInfo;
    renderInfo.pDepthAttachment = &depthInfo;

    vkCmdBeginRendering(cmd, &renderInfo);

    drawObjects(cmd);

    vkCmdEndRendering(cmd);

    imageLayoutTransition(cmd, currentImage.image,
                               VK_IMAGE_ASPECT_COLOR_BIT,
                               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                               VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                               0,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit = submitInfo(&cmd);
    submit.pWaitDstStageMask = &waitStage;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &currentFrame.imageAcquiredSemaphore;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &currentImage.renderSemaphore;

    VK_CHECK(vkQueueSubmit(m_ctx->graphicsQueue(), 1, &submit, currentFrame.renderFence));

    VkPresentInfoKHR present = presentInfo();
    present.swapchainCount = 1;
    present.pSwapchains = &m_swapchain;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &currentImage.renderSemaphore;
    present.pImageIndices = &swapchainImageIndex;

    VK_CHECK(vkQueuePresentKHR(m_ctx->graphicsQueue(), &present));

    ++m_frameCount;
}

void VKlelu::drawObjects(VkCommandBuffer cmd)
{
    FrameData &currentFrame = getCurrentFrame();

    glm::vec3 camera = { 0.0f, 0.0f, -5.0f };
    m_sceneParameters.cameraPos = glm::vec4{ camera, 1.0f };
    glm::mat4 view = glm::translate(glm::mat4(1.0f), camera);
    glm::mat4 projection = glm::perspective(glm::radians(70.0f), static_cast<float>(m_fbSize.width)/static_cast<float>(m_fbSize.height), 0.1f, 200.0f);
    projection[1][1] *= -1;

    CameraData cam;
    cam.proj = projection;
    cam.view = view;
    cam.viewProj = projection * view;

    void *camData = currentFrame.cameraBufferMapping;
    memcpy(camData, &cam, sizeof(cam));

    char *sceneData = (char *)m_sceneParameterBufferMapping;
    int frameIndex = m_frameCount % MAX_FRAMES_IN_FLIGHT;
    sceneData += sizeof(SceneData) * frameIndex;
    memcpy(sceneData, &m_sceneParameters, sizeof(SceneData));

    void *objData = currentFrame.objectBufferMapping;;
    ObjectData *objectSSBO = (ObjectData *)objData;
    for (size_t i = 0; i < m_himmelit.size(); ++i) {
        objectSSBO[i].model = m_himmelit[i].translate * m_himmelit[i].rotate * m_himmelit[i].scale;
        objectSSBO[i].normalMat = glm::transpose(glm::inverse(objectSSBO[i].model));
    }

    Mesh *lastMesh = nullptr;
    Material *lastMaterial = nullptr;

    for (size_t i = 0; i < m_himmelit.size(); ++i) {
        Himmeli &himmeli = m_himmelit[i];
        if (!himmeli.material || !himmeli.mesh)
            continue;

        if (himmeli.material != lastMaterial) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, himmeli.material->pipeline);
            lastMaterial = himmeli.material;
            uint32_t uniformOffset = static_cast<uint32_t>(sizeof(SceneData)) * frameIndex;
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
            VkBuffer vertexBuffer = himmeli.mesh->vertexBuffer->buffer();
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(cmd, himmeli.mesh->indexBuffer->buffer(), 0, VK_INDEX_TYPE_UINT32);
            lastMesh = himmeli.mesh;
        }
        vkCmdDrawIndexed(cmd, himmeli.mesh->numIndices, 1, 0, 0, 0);
    }
}

FrameData &VKlelu::getCurrentFrame()
{
    return m_frameData[m_frameCount % MAX_FRAMES_IN_FLIGHT];
}

void VKlelu::initScene()
{
    ObjFile monkeyObj("suzanne.obj");
    uploadMesh(monkeyObj, "monkey");

    ImageFile monkeyTexture("suzanne_uv.png");
    uploadImage(monkeyTexture, "monkey_diffuse");

    createMaterial(m_meshPipeline, m_meshPipelineLayout, "monkey_material");

    Himmeli monkey;
    monkey.mesh = getMesh("monkey");
    monkey.material = getMaterial("monkey_material");
    monkey.scale = glm::mat4{ 1.0f };
    monkey.rotate = glm::mat4{ 1.0f };
    monkey.translate = glm::mat4{ 1.0f };
    m_himmelit.push_back(monkey);

    Material *monkeyMat = getMaterial("monkey_material");

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.pNext = nullptr;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_singleTextureSetLayout;
    VK_CHECK(vkAllocateDescriptorSets(m_device, &allocInfo, &monkeyMat->textureSet));

    VkSamplerCreateInfo samplerInfo = samplerCreateInfo(VK_FILTER_LINEAR);
    VK_CHECK(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_linearSampler));
    deferCleanup([=](){ vkDestroySampler(m_device, m_linearSampler, nullptr); });

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageView = m_textures["monkey_diffuse"].imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet texture = writeDescriptorImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, monkeyMat->textureSet, &imageInfo, 0);

    VkDescriptorImageInfo imageSamplerInfo = {};
    imageSamplerInfo.sampler = m_linearSampler;
    VkWriteDescriptorSet sampler = writeDescriptorImage(VK_DESCRIPTOR_TYPE_SAMPLER, monkeyMat->textureSet, &imageSamplerInfo, 1);

    VkWriteDescriptorSet writeSets[] = { texture, sampler };
    vkUpdateDescriptorSets(m_device, 2, &writeSets[0], 0, nullptr);

    m_sceneParameters.lightPos = { -1.0f, 1.0f, 5.0f, 0.0f };
    m_sceneParameters.lightColor = { 1.0f, 1.0f, 1.0f, 0.0f };
}

Material *VKlelu::createMaterial(VkPipeline pipeline, VkPipelineLayout layout, const std::string name)
{
    Material mat;
    mat.pipeline = pipeline;
    mat.pipelineLayout = layout;
    m_materials[name] = mat;
    return &m_materials[name];
}

Mesh *VKlelu::getMesh(const std::string name)
{
    auto it = m_meshes.find(name);
    if (it == m_meshes.end())
        return nullptr;
    return &(*it).second;
}

Material *VKlelu::getMaterial(const std::string name)
{
    auto it = m_materials.find(name);
    if (it == m_materials.end())
        return nullptr;
    return &(*it).second;
}

void VKlelu::uploadMesh(ObjFile &obj, std::string name)
{
    Mesh mesh;
    mesh.numVertices = static_cast<uint32_t>(obj.vertices.size());
    mesh.numIndices = static_cast<uint32_t>(obj.indices.size());
    size_t vertexBufferSize = mesh.numVertices * sizeof(Vertex);
    size_t indexBufferSize = mesh.numIndices * sizeof(uint32_t);

    auto stagingBuffer = m_ctx->allocateBuffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void *data = stagingBuffer->map();
    memcpy(data, obj.vertices.data(), vertexBufferSize);
    memcpy((uint8_t *)data + vertexBufferSize, obj.indices.data(), indexBufferSize);

    mesh.vertexBuffer = m_ctx->allocateBuffer(vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    mesh.indexBuffer = m_ctx->allocateBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy copy;
        copy.dstOffset = 0;
        copy.srcOffset = 0;
        copy.size = vertexBufferSize;
        vkCmdCopyBuffer(cmd, stagingBuffer->buffer(), mesh.vertexBuffer->buffer(), 1, &copy);
        copy.srcOffset = vertexBufferSize;
        copy.size = indexBufferSize;
        vkCmdCopyBuffer(cmd, stagingBuffer->buffer(), mesh.indexBuffer->buffer(), 1, &copy);
    });

    m_meshes[name] = std::move(mesh);
}

void VKlelu::uploadImage(ImageFile &image, std::string name)
{
    Texture texture;
    VkDeviceSize imageSize = image.width * image.height * 4;
    VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;

    auto stagingBuffer = m_ctx->allocateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void *data = stagingBuffer->map();
    memcpy(data, image.pixels, imageSize);

    VkExtent3D imageExtent;
    imageExtent.width = image.width;
    imageExtent.height = image.height;
    imageExtent.depth = 1;

    texture.image = m_ctx->allocateImage(imageExtent, imageFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    immediateSubmit([&](VkCommandBuffer cmd) {
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
        barrier.image = texture.image->image();
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

        vkCmdCopyBufferToImage(cmd, stagingBuffer->buffer(), texture.image->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        VkImageMemoryBarrier barrier2 = barrier;
        barrier2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier2);
    });

    texture.imageView = texture.image->createImageView(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
    m_textures[name] = std::move(texture);
}

void VKlelu::immediateSubmit(std::function<void(VkCommandBuffer)> &&function)
{
    VkCommandBuffer cmd = m_uploadContext.commandBuffer;
    VkCommandBufferBeginInfo cmdBeginInfo = commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submit = submitInfo(&cmd);

    VK_CHECK(vkQueueSubmit(m_ctx->graphicsQueue(), 1, &submit, m_uploadContext.uploadFence));

    vkWaitForFences(m_device, 1, &m_uploadContext.uploadFence, true, UINT64_MAX);
    vkResetFences(m_device, 1, &m_uploadContext.uploadFence);

    vkResetCommandPool(m_device, m_uploadContext.commandPool, 0);
}

void VKlelu::loadShader(const char *path, VkShaderModule &module)
{
    Path fullPath = getShaderPath(path);

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

    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module: " + std::string(path));
    }
}

void VKlelu::deferCleanup(std::function<void()> &&cleanupFunc)
{
    m_resourceJanitor.push_back(cleanupFunc);
}
