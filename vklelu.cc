#include "vklelu.hh"

#include "utils.hh"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "SDL.h"
#include "SDL_vulkan.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include "VkBootstrap.h"
#include "vulkan/vulkan.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768

#define REQUIRED_VK_VERSION_MINOR 3

#define MAX_OBJECTS 10000

VKlelu::VKlelu(int argc, char *argv[]):
    frameCount(0),
    window(nullptr),
    instance(nullptr),
    surface(nullptr),
    device(nullptr),
    allocator(nullptr)
{
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "Failed to init SDL\n");
        return;
    }

    window = SDL_CreateWindow("VKlelu",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              WINDOW_WIDTH, WINDOW_HEIGHT,
                              SDL_WINDOW_VULKAN);
    if (!window) {
        fprintf(stderr, "Failed to create SDL window\n");
        return;
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
    vkDeviceWaitIdle(device);

    for (auto destroyfn : resourceJanitor) {
        destroyfn();
    }

    if (allocator)
        vmaDestroyAllocator(allocator);

    if (device)
        vkDestroyDevice(device, nullptr);

    if (surface)
        vkDestroySurfaceKHR(instance, surface, nullptr);

    if (instance) {
        vkb::destroy_debug_utils_messenger(instance, debugMessenger);
        vkDestroyInstance(instance, nullptr);
    }

    if (window)
        SDL_DestroyWindow(window);

    SDL_Quit();
}

int VKlelu::run()
{
    if (!window)
        return EXIT_FAILURE;

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
    FrameData currentFrame = get_current_frame();

    VK_CHECK(vkWaitForFences(device, 1, &currentFrame.renderFence, true, NS_IN_SEC));
    VK_CHECK(vkResetFences(device, 1, &currentFrame.renderFence));

    uint32_t swapchainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(device, swapchain, NS_IN_SEC, currentFrame.imageAcquiredSemaphore, nullptr, &swapchainImageIndex));

    VK_CHECK(vkResetCommandBuffer(currentFrame.mainCommandBuffer, 0));

    VkCommandBuffer cmd = currentFrame.mainCommandBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    VkClearValue clearValue;
    float flash = abs(sin(frameCount / 120.0f));
    clearValue.color = { { 0.0f, 0.0f, flash, 1.0f } };

    VkClearValue depthClearValue;
    depthClearValue.depthStencil.depth = 1.0f;

    VkClearValue clearValues[2] = { clearValue, depthClearValue };

    VkRenderPassBeginInfo rpInfo = renderpass_begin_info(renderPass, fbSize, framebuffers[swapchainImageIndex]);
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = &clearValues[0];

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    draw_objects(cmd, himmelit.data(), himmelit.size());

    vkCmdEndRenderPass(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit = submit_info(&cmd);
    submit.pWaitDstStageMask = &waitStage;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &currentFrame.imageAcquiredSemaphore;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &currentFrame.renderSemaphore;

    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submit, currentFrame.renderFence));

    VkPresentInfoKHR presentInfo = present_info();
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &currentFrame.renderSemaphore;
    presentInfo.pImageIndices = &swapchainImageIndex;

    VK_CHECK(vkQueuePresentKHR(graphicsQueue, &presentInfo));

    ++frameCount;
}

void VKlelu::draw_objects(VkCommandBuffer cmd, Himmeli *first, int count)
{
    FrameData currentFrame = get_current_frame();

    glm::vec3 camera = { 0.0f, 0.0f, -5.0f };
    sceneParameters.cameraPos = glm::vec4{ camera, 1.0f };
    glm::mat4 view = glm::translate(glm::mat4(1.0f), camera);
    glm::mat4 projection = glm::perspective(glm::radians(70.0f), (float)fbSize.width/(float)fbSize.height, 0.1f, 200.0f);
    projection[1][1] *= -1;

    CameraData cam;
    cam.proj = projection;
    cam.view = view;
    cam.viewproj = projection * view;

    void *camData;
    vmaMapMemory(allocator, currentFrame.cameraBuffer.allocation, &camData);
    memcpy(camData, &cam, sizeof(cam));
    vmaUnmapMemory(allocator, currentFrame.cameraBuffer.allocation);

    char *sceneData;
    vmaMapMemory(allocator, sceneParameterBuffer.allocation, (void**)&sceneData);
    int frameIndex = frameCount % MAX_FRAMES_IN_FLIGHT;
    sceneData += pad_uniform_buffer_size(sizeof(SceneData)) * frameIndex;
    memcpy(sceneData, &sceneParameters, sizeof(SceneData));
    vmaUnmapMemory(allocator, sceneParameterBuffer.allocation);

    void *objData;
    vmaMapMemory(allocator, currentFrame.objectBuffer.allocation, &objData);
    ObjectData * objectSSBO = (ObjectData *)objData;
    for (int i = 0; i < count; ++i) {
        Himmeli &himmeli = first[i];
        objectSSBO[i].model = himmeli.translate * himmeli.rotate * himmeli.scale;
    }
    vmaUnmapMemory(allocator, currentFrame.objectBuffer.allocation);

    Mesh *lastMesh = nullptr;
    Material *lastMaterial = nullptr;

    for (int i = 0; i < count; ++i) {
        Himmeli &himmeli = first[i];
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
            vkCmdBindVertexBuffers(cmd, 0, 1, &himmeli.mesh->vertexBuffer.buffer, &offset);
            lastMesh = himmeli.mesh;
        }
        vkCmdDraw(cmd, himmeli.mesh->vertices.size(), 1, 0, 0);
    }
}

void VKlelu::update()
{
    for (Himmeli &himmeli : himmelit) {
        himmeli.rotate = glm::rotate(glm::mat4{ 1.0f }, glm::radians(frameCount * 0.4f), glm::vec3(0, 1, 0));
    }
}

FrameData &VKlelu::get_current_frame()
{
    return frameData[frameCount % MAX_FRAMES_IN_FLIGHT];
}

void VKlelu::set_runtime_dirs()
{
    wdIsBuildDir = wd_is_builddir();

    char *ass = getenv("VKLELU_ASSETDIR");
    assetDir = ass ? ass : wdIsBuildDir ? ".." : ".";
    assetDir.push_back('/');
    fprintf(stderr, "Asset directory: %s\n", assetDir.c_str());

    char *sdr = getenv("VKLELU_SHADERDIR");
    shaderDir = sdr ? sdr : ".";
    shaderDir.push_back('/');
    fprintf(stderr, "Shader directory: %s\n", shaderDir.c_str());
}

bool VKlelu::wd_is_builddir()
{
    FILE *f = fopen("CMakeCache.txt", "r");
    if (!f) {
        return false;
    } else {
        fclose(f);
        fprintf(stderr, "Running from build directory\n");
        return true;
    }
}

void VKlelu::init_scene()
{
    load_meshes();
    load_images();

    Himmeli kapina;
    kapina.mesh = get_mesh("kapina");
    kapina.material = get_material("defaultMesh");
    kapina.scale = glm::mat4{ 1.0f };
    kapina.rotate = glm::mat4{ 1.0f };
    kapina.translate = glm::mat4{ 1.0f };
    himmelit.push_back(kapina);

    Himmeli triangle;
    triangle.mesh = get_mesh("triangle");
    triangle.material = get_material("defaultMesh");
    triangle.scale = glm::scale(glm::mat4{ 1.0f }, glm::vec3{ 0.2f, 0.2f, 0.2f });
    triangle.rotate = glm::mat4{ 1.0f };
    triangle.translate = glm::translate(glm::mat4{ 1.0f }, glm::vec3(-5.0f, 0.0f, -5.0f));
    himmelit.push_back(triangle);

    VkSamplerCreateInfo samplerInfo = sampler_create_info(VK_FILTER_NEAREST);
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &nearestSampler));
    resourceJanitor.push_back([=](){ vkDestroySampler(device, nearestSampler, nullptr); });

    Material *defaultMat = get_material("defaultMesh");

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.pNext = nullptr;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &singleTextureSetLayout;
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &defaultMat->textureSet));

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.sampler = nearestSampler;
    imageInfo.imageView = textures["kapina_diffuse"].imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet texture = write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, defaultMat->textureSet, &imageInfo, 0);

    vkUpdateDescriptorSets(device, 1, &texture, 0, nullptr);

    sceneParameters.lightPos = { 10.0f, 10.0f, 10.0f, 0.0f };
    sceneParameters.lightColor = { 1.0f, 1.0f, 1.0f, 0.0f };
}

void VKlelu::load_meshes()
{
    Mesh triangleMesh;
    triangleMesh.vertices.resize(3);
    triangleMesh.vertices[0].position = { 1.0f, 1.0f, 0.0f };
    triangleMesh.vertices[1].position = { -1.0f, 1.0f, 0.0f };
    triangleMesh.vertices[2].position = { 0.0f, -1.0f, 0.0f };
    triangleMesh.vertices[0].color = { 1.0f, 0.0f, 0.0f };
    triangleMesh.vertices[1].color = { 0.0f, 1.0f, 0.0f };
    triangleMesh.vertices[2].color = { 0.0f, 0.0f, 1.0f };
    upload_mesh(triangleMesh);
    meshes["triangle"] = triangleMesh;
    resourceJanitor.push_back([=](){ vmaDestroyBuffer(allocator, triangleMesh.vertexBuffer.buffer, triangleMesh.vertexBuffer.allocation); });

    Mesh kapinaMesh;
    kapinaMesh.load_obj_file("kultainenapina.obj", assetDir.c_str());
    upload_mesh(kapinaMesh);
    meshes["kapina"] = kapinaMesh;
    resourceJanitor.push_back([=](){ vmaDestroyBuffer(allocator, kapinaMesh.vertexBuffer.buffer, kapinaMesh.vertexBuffer.allocation); });
}

void VKlelu::upload_mesh(Mesh &mesh)
{
    size_t bufferSize = mesh.vertices.size() * sizeof(Vertex);

    VkBufferCreateInfo stagingBufferInfo = {};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.pNext = nullptr;
    stagingBufferInfo.size = bufferSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    BufferAllocation stagingBuffer;

    VK_CHECK(vmaCreateBuffer(allocator, &stagingBufferInfo, &vmaAllocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr));

    void *data;
    vmaMapMemory(allocator, stagingBuffer.allocation, &data);
    memcpy(data, mesh.vertices.data(), bufferSize);
    vmaUnmapMemory(allocator, stagingBuffer.allocation);

    VkBufferCreateInfo vertexBufferInfo = {};
    vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexBufferInfo.pNext = nullptr;
    vertexBufferInfo.size = bufferSize;
    vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VK_CHECK(vmaCreateBuffer(allocator, &vertexBufferInfo, &vmaAllocInfo, &mesh.vertexBuffer.buffer, &mesh.vertexBuffer.allocation, nullptr));

    immediate_submit([=](VkCommandBuffer cmd) {
        VkBufferCopy copy;
        copy.dstOffset = 0;
        copy.srcOffset = 0;
        copy.size = bufferSize;
        vkCmdCopyBuffer(cmd, stagingBuffer.buffer, mesh.vertexBuffer.buffer, 1, &copy);
    });

    vmaDestroyBuffer(allocator, stagingBuffer.buffer, stagingBuffer.allocation);
}

void VKlelu::immediate_submit(std::function<void(VkCommandBuffer cmad)> &&function)
{
    VkCommandBuffer cmd = uploadContext.commandBuffer;
    VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo = submit_info(&cmd);

    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, uploadContext.uploadFence));

    vkWaitForFences(device, 1, &uploadContext.uploadFence, true, UINT64_MAX);
    vkResetFences(device, 1, &uploadContext.uploadFence);

    vkResetCommandPool(device, uploadContext.commandPool, 0);
}

void VKlelu::load_images()
{
    Texture kapina;

    load_image("kultainenapina.jpg", kapina.image);

    VkImageViewCreateInfo viewInfo = imageview_create_info(VK_FORMAT_R8G8B8A8_SRGB, kapina.image.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &kapina.imageView));

    resourceJanitor.push_back([=](){
        vmaDestroyImage(allocator, kapina.image.image, kapina.image.allocation);
        vkDestroyImageView(device, kapina.imageView, nullptr);
    });

    textures["kapina_diffuse"] = kapina;
}

bool VKlelu::load_image(const char *path, ImageAllocation &image)
{
    int width;
    int height;
    int channels;

    std::string fullPath = assetDir + std::string(path);

    stbi_uc *pixels = stbi_load(fullPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        fprintf(stderr, "Failed to load image: %s\n", path);
        return false;
    }

    void *pixelPtr = pixels;
    VkDeviceSize imageSize = width * height * 4;
    VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;

    BufferAllocation stagingBuffer = create_buffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void *data;
    vmaMapMemory(allocator, stagingBuffer.allocation, &data);
    memcpy(data, pixelPtr, imageSize);
    vmaUnmapMemory(allocator, stagingBuffer.allocation);

    stbi_image_free(pixels);

    VkExtent3D imageExtent;
    imageExtent.width = width;
    imageExtent.height = height;
    imageExtent.depth = 1;

    ImageAllocation newImage = create_image(imageExtent, imageFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

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
        barrier.image = newImage.image;
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

        vkCmdCopyBufferToImage(cmd, stagingBuffer.buffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        VkImageMemoryBarrier barrier2 = barrier;
        barrier2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier2);
    });

    vmaDestroyBuffer(allocator, stagingBuffer.buffer, stagingBuffer.allocation);

    image = newImage;

    return true;
}

bool VKlelu::load_shader(const char *path, VkShaderModule &module)
{
    std::string fullPath = shaderDir + std::string(path);

    FILE *f = fopen(fullPath.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "Failed to open file: %s\n", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<uint32_t> spv_data(fileSize / sizeof(uint32_t));
    size_t ret = fread(&spv_data[0], sizeof(spv_data[0]), spv_data.size(), f);
    fclose(f);

    if ((ret * sizeof(uint32_t)) != fileSize) {
        fprintf(stderr, "Failed to read file: %s\n", path);
        return false;
    }

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext = nullptr;
    createInfo.codeSize = spv_data.size() * sizeof(uint32_t);
    createInfo.pCode = &spv_data[0];

    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create shader module: %s\n", path);
        return false;
    }

    return true;
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

BufferAllocation VKlelu::create_buffer(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = size;
    bufferInfo.usage = usage;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;

    BufferAllocation buffer;
    VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer.buffer, &buffer.allocation, nullptr));
    return buffer;
}

ImageAllocation VKlelu::create_image(VkExtent3D extent, VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VmaMemoryUsage memoryUsage)
{
    VkImageCreateInfo imgInfo = image_create_info(format, usage, extent);
    imgInfo.samples = samples;

    VmaAllocationCreateInfo imgAllocInfo = {};
    imgAllocInfo.usage = memoryUsage;
    imgAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    ImageAllocation image;
    vmaCreateImage(allocator, &imgInfo, &imgAllocInfo, &image.image, &image.allocation, nullptr);
    return image;
}

size_t VKlelu::pad_uniform_buffer_size(size_t originalSize)
{
    size_t minUboAllignment = physicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
    size_t alignedSize = originalSize;
    if (minUboAllignment > 0)
        alignedSize = (alignedSize + minUboAllignment - 1) & ~(minUboAllignment -1);
    return alignedSize;
}

bool VKlelu::init_vulkan()
{
    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("VKlelu")
                           .request_validation_layers()
                           .require_api_version(1, REQUIRED_VK_VERSION_MINOR)
                           .use_default_debug_messenger()
                           .build();
    if (!inst_ret) {
        fprintf(stderr, "Failed to create Vulkan instance. Error: %s\n", inst_ret.error().message().c_str());
        return false;
    }
    vkb::Instance vkb_inst = inst_ret.value();
    instance = vkb_inst.instance;
    debugMessenger = vkb_inst.debug_messenger;

    if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
        fprintf(stderr, "Failed to create Vulkan surface");
        return false;
    }

    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    auto phys_ret = selector.set_surface(surface)
                            .set_minimum_version(1, REQUIRED_VK_VERSION_MINOR)
                            .select();
    if (!phys_ret) {
        fprintf(stderr, "Failed to select Vulkan physical device. Error: %s\n", phys_ret.error().message().c_str());
        return false;
    }
    vkb::PhysicalDevice vkb_phys = phys_ret.value();
    physicalDevice = vkb_phys.physical_device;
    physicalDeviceProperties = vkb_phys.properties;

    msaaLevel = VK_SAMPLE_COUNT_4_BIT;

    VkPhysicalDeviceShaderDrawParametersFeatures shaderParamFeatures = {};
    shaderParamFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
    shaderParamFeatures.pNext = nullptr;
    shaderParamFeatures.shaderDrawParameters = VK_TRUE;

    vkb::DeviceBuilder deviceBuilder{ vkb_phys };
    auto dev_ret = deviceBuilder.add_pNext(&shaderParamFeatures)
                                .build();
    if (!dev_ret) {
        fprintf(stderr, "Failed to create Vulkan device. Error: %s\n", dev_ret.error().message().c_str());
        return false;
    }
    vkb::Device vkb_device = dev_ret.value();
    device = vkb_device.device;

    auto graphics_queue_ret  = vkb_device.get_queue(vkb::QueueType::graphics);
    if (!graphics_queue_ret) {
        fprintf(stderr, "Failed to get graphics queue. Error: %s\n", graphics_queue_ret.error().message().c_str());
        return false;
    }
    graphicsQueue = graphics_queue_ret.value();
    graphicsQueueFamily = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.instance = instance;
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &allocator));

    fprintf(stderr, "Vulkan 1.%d initialized successfully\n", REQUIRED_VK_VERSION_MINOR);

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
    vkb::SwapchainBuilder swapchainBuilder{physicalDevice, device, surface};
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

    resourceJanitor.push_back([=](){ vkDestroySwapchainKHR(device, swapchain, nullptr); });

    VkExtent3D imageExtent = {
        fbSize.width,
        fbSize.height,
        1
    };
    depthImageFormat = VK_FORMAT_D32_SFLOAT;

    colorImage = create_image(imageExtent, swapchainImageFormat, msaaLevel, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    VkImageViewCreateInfo imageViewInfo = imageview_create_info(swapchainImageFormat, colorImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(device, &imageViewInfo, nullptr, &colorImageView));

    depthImage = create_image(imageExtent, depthImageFormat, msaaLevel, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    VkImageViewCreateInfo depthViewInfo = imageview_create_info(depthImageFormat, depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
    VK_CHECK(vkCreateImageView(device, &depthViewInfo, nullptr, &depthImageView));

    resourceJanitor.push_back([=](){
        vmaDestroyImage(allocator, colorImage.image, colorImage.allocation);
        vkDestroyImageView(device, colorImageView, nullptr);
        vmaDestroyImage(allocator, depthImage.image, depthImage.allocation);
        vkDestroyImageView(device, depthImageView, nullptr);
    });

    fprintf(stderr, "Swapchain initialized successfully\n");
    return true;
}

bool VKlelu::init_commands()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkCommandPoolCreateInfo commandPoolInfo = command_pool_create_info(graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &frameData[i].commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo = command_buffer_allocate_info(frameData[i].commandPool, 1);
        VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &frameData[i].mainCommandBuffer));

        resourceJanitor.push_back([=](){ vkDestroyCommandPool(device, frameData[i].commandPool, nullptr); });
    }

    VkCommandPoolCreateInfo uploadPoolInfo = command_pool_create_info(graphicsQueueFamily);
    VK_CHECK(vkCreateCommandPool(device, &uploadPoolInfo, nullptr, &uploadContext.commandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = command_buffer_allocate_info(uploadContext.commandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &uploadContext.commandBuffer));

    resourceJanitor.push_back([=](){ vkDestroyCommandPool(device, uploadContext.commandPool, nullptr); });

    fprintf(stderr, "Command pool initialized successfully\n");
    return true;
}

bool VKlelu::init_default_renderpass()
{
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = swapchainImageFormat;
    colorAttachment.samples = msaaLevel;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment = {};
    depthAttachment.flags = 0;
    depthAttachment.format = depthImageFormat;
    depthAttachment.samples = msaaLevel;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription colorAttachmentResolve = {};
    colorAttachmentResolve.format = swapchainImageFormat;
    colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentResolveRef = {};
    colorAttachmentResolveRef.attachment = 2;
    colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
    subpass.pResolveAttachments = &colorAttachmentResolveRef;

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

    VkAttachmentDescription attachments[3] = { colorAttachment, depthAttachment, colorAttachmentResolve };
    VkSubpassDependency dependencies[2] = { dependency, depthDependency };

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 3;
    renderPassInfo.pAttachments = &attachments[0];
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = &dependencies[0];

    VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));

    resourceJanitor.push_back([=](){ vkDestroyRenderPass(device, renderPass, nullptr); });

    fprintf(stderr, "Renderpass initialized successfully\n");
    return true;
}

bool VKlelu::init_framebuffers()
{
    VkFramebufferCreateInfo fbInfo = framebuffer_create_info(renderPass, fbSize);

    uint32_t swapchainImageCount = swapchainImages.size();
    framebuffers = std::vector<VkFramebuffer>(swapchainImageCount);

    for (int i = 0; i < swapchainImageCount; ++i) {
        VkImageView attachments[3] = { colorImageView, depthImageView, swapchainImageViews[i] };
        fbInfo.attachmentCount = 3;
        fbInfo.pAttachments = &attachments[0];
        VK_CHECK(vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffers[i]));

        resourceJanitor.push_back([=](){
            vkDestroyImageView(device, swapchainImageViews[i], nullptr);
            vkDestroyFramebuffer(device, framebuffers[i], nullptr);
        });
    }

    fprintf(stderr, "Framebuffers initialized successfully\n");
    return true;
}

bool VKlelu::init_sync_structures()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkFenceCreateInfo fenceCreateInfo = fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
        VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &frameData[i].renderFence));

        VkSemaphoreCreateInfo semaphoreCreateInfo = semaphore_create_info();
        VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frameData[i].imageAcquiredSemaphore));
        VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frameData[i].renderSemaphore));

        resourceJanitor.push_back([=](){
            vkDestroyFence(device, frameData[i].renderFence, nullptr);
            vkDestroySemaphore(device, frameData[i].imageAcquiredSemaphore, nullptr);
            vkDestroySemaphore(device, frameData[i].renderSemaphore, nullptr);
        });
    }

    VkFenceCreateInfo uploadFenceInfo = fence_create_info();
    VK_CHECK(vkCreateFence(device, &uploadFenceInfo, nullptr, &uploadContext.uploadFence));
    resourceJanitor.push_back([=](){ vkDestroyFence(device, uploadContext.uploadFence, nullptr); });

    fprintf(stderr, "Sync structures initialized successfully\n");
    return true;
}

bool VKlelu::init_descriptors()
{
    size_t sceneParamBufferSize = MAX_FRAMES_IN_FLIGHT * pad_uniform_buffer_size(sizeof(SceneData));
    sceneParameterBuffer = create_buffer(sceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    resourceJanitor.push_back([=](){ vmaDestroyBuffer(allocator, sceneParameterBuffer.buffer, sceneParameterBuffer.allocation); });

    VkDescriptorSetLayoutBinding camBind = descriptor_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
    VkDescriptorSetLayoutBinding sceneBind = descriptor_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1);
    VkDescriptorSetLayoutBinding bindings[2] = { camBind, sceneBind };

    VkDescriptorSetLayoutCreateInfo setInfo = {};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.pNext = nullptr;
    setInfo.flags = 0;
    setInfo.bindingCount = 2;
    setInfo.pBindings = bindings;

    VK_CHECK(vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &globalSetLayout));

    resourceJanitor.push_back([=](){ vkDestroyDescriptorSetLayout(device, globalSetLayout, nullptr); });

    VkDescriptorSetLayoutBinding objectBind = descriptor_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);

    VkDescriptorSetLayoutCreateInfo set2Info = {};
    set2Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set2Info.pNext = nullptr;
    set2Info.flags = 0;
    set2Info.bindingCount = 1;
    set2Info.pBindings = &objectBind;

    VK_CHECK(vkCreateDescriptorSetLayout(device, &set2Info, nullptr, &objectSetLayout));

    resourceJanitor.push_back([=](){ vkDestroyDescriptorSetLayout(device, objectSetLayout, nullptr); });

    VkDescriptorSetLayoutBinding textureBind = descriptor_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);

    VkDescriptorSetLayoutCreateInfo set3Info = {};
    set3Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set3Info.pNext = nullptr;
    set3Info.bindingCount = 1;
    set3Info.flags = 0;
    set3Info.pBindings = &textureBind;

    VK_CHECK(vkCreateDescriptorSetLayout(device, &set3Info, nullptr, &singleTextureSetLayout));

    resourceJanitor.push_back([=](){ vkDestroyDescriptorSetLayout(device, singleTextureSetLayout, nullptr); });

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

    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));

    resourceJanitor.push_back([=](){ vkDestroyDescriptorPool(device, descriptorPool, nullptr); });

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        frameData[i].cameraBuffer = create_buffer(sizeof(CameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        frameData[i].objectBuffer = create_buffer(sizeof(ObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        resourceJanitor.push_back([=](){
            vmaDestroyBuffer(allocator, frameData[i].cameraBuffer.buffer, frameData[i].cameraBuffer.allocation);
            vmaDestroyBuffer(allocator, frameData[i].objectBuffer.buffer, frameData[i].objectBuffer.allocation);
        });

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.pNext = nullptr;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &globalSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &frameData[i].globalDescriptor));

        VkDescriptorSetAllocateInfo objAllocInfo = {};
        objAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        objAllocInfo.pNext = nullptr;
        objAllocInfo.descriptorPool = descriptorPool;
        objAllocInfo.descriptorSetCount = 1;
        objAllocInfo.pSetLayouts = &objectSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(device, &objAllocInfo, &frameData[i].objectDescriptor));

        VkDescriptorBufferInfo camInfo = {};
        camInfo.buffer = frameData[i].cameraBuffer.buffer;
        camInfo.offset = 0;
        camInfo.range = sizeof(CameraData);

        VkDescriptorBufferInfo sceneInfo = {};
        sceneInfo.buffer = sceneParameterBuffer.buffer;
        sceneInfo.offset = 0;
        sceneInfo.range = sizeof(SceneData);

        VkDescriptorBufferInfo objInfo = {};
        objInfo.buffer = frameData[i].objectBuffer.buffer;
        objInfo.offset = 0;
        objInfo.range = sizeof(ObjectData) * MAX_OBJECTS;

        VkWriteDescriptorSet camWrite = write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frameData[i].globalDescriptor, &camInfo, 0);
        VkWriteDescriptorSet sceneWrite = write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, frameData[i].globalDescriptor, &sceneInfo, 1);
        VkWriteDescriptorSet objWrite = write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frameData[i].objectDescriptor, &objInfo, 0);
        VkWriteDescriptorSet writeSet[3] = { camWrite, sceneWrite, objWrite };
        vkUpdateDescriptorSets(device, 3, writeSet, 0 , nullptr);
    }

    fprintf(stderr, "Descriptors initialized successfully\n");
    return true;
}

bool VKlelu::init_pipelines()
{
    VkShaderModule fragShader;
    if (!load_shader("shader.frag.spv", fragShader))
        return false;
    fprintf(stderr, "Fragment shader module shader.frag.spv created successfully\n");

    VkShaderModule vertShader;
    if (!load_shader("shader.vert.spv", vertShader))
        return false;
    fprintf(stderr, "Vertex shader module shader.vert.spv created successfully\n");

    VkPipeline meshPipeline;
    VkPipelineLayout meshPipelineLayout;

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
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &meshPipelineLayout));

    resourceJanitor.push_back([=](){ vkDestroyPipelineLayout(device, meshPipelineLayout, nullptr); });

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
    builder.multisampling.rasterizationSamples = msaaLevel;
    builder.viewport.x = 0.0f;
    builder.viewport.y = 0.0f;
    builder.viewport.width = fbSize.width;
    builder.viewport.height = fbSize.height;
    builder.viewport.minDepth = 0.0f;
    builder.viewport.maxDepth = 1.0f;
    builder.scissor.offset = { 0, 0 };
    builder.scissor.extent = fbSize;
    builder.pipelineLayout = meshPipelineLayout;
    meshPipeline = builder.build_pipeline(device, renderPass);

    resourceJanitor.push_back([=](){ vkDestroyPipeline(device, meshPipeline, nullptr); });

    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);

    if (!meshPipeline) {
        fprintf(stderr, "Failed to create graphics pipeline \"mesh\"\n");
        return false;
    }

    create_material(meshPipeline, meshPipelineLayout, "defaultMesh");

    fprintf(stderr, "Graphics pipelines initialized successfully\n");
    return true;
}
