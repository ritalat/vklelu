#include "vklelu.hh"

#include "utils.hh"

#include "SDL.h"
#include "SDL_vulkan.h"
#include "VkBootstrap.h"
#include "vulkan/vulkan.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768

#define REQUIRED_VK_VERSION_MINOR 3

VKlelu::VKlelu(int argc, char *argv[]):
    frameCount(0),
    currentShader(0),
    window(nullptr),
    instance(nullptr),
    surface(nullptr),
    device(nullptr),
    swapchain(nullptr)
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

    fprintf(stderr, "Window size: %ux%u\n", WINDOW_WIDTH, WINDOW_HEIGHT);
}

VKlelu::~VKlelu()
{
    vkDeviceWaitIdle(device);

    vkDestroyPipelineLayout(device, trianglePipelineLayout, nullptr);
    vkDestroyPipeline(device, rgbTrianglePipeline, nullptr);
    vkDestroyPipeline(device, trianglePipeline, nullptr);

    vkDestroySemaphore(device, imageAcquiredSemaphore, nullptr);
    vkDestroySemaphore(device, renderSemaphore, nullptr);
    vkDestroyFence(device, renderFence, nullptr);

    vkDestroyRenderPass(device, renderPass, nullptr);

    vkDestroyCommandPool(device, commandPool, nullptr);

    if (swapchain) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        for (int i = 0; i < swapchainImageViews.size(); ++i) {
            vkDestroyFramebuffer(device, framebuffers[i], nullptr);
            vkDestroyImageView(device, swapchainImageViews[i], nullptr);
        }
    }

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

    bool quit = false;
    SDL_Event event;

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    quit = true;
                    break;
                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                        case SDLK_SPACE:
                            ++currentShader;
                            if (currentShader > 1)
                                currentShader = 0;
                            break;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }
        }

        draw();
    }

    return EXIT_SUCCESS;
}

void VKlelu::draw()
{
    VK_CHECK(vkWaitForFences(device, 1, &renderFence, true, 1000000000));
    VK_CHECK(vkResetFences(device, 1, &renderFence));

    uint32_t swapchainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(device, swapchain, 1000000000, imageAcquiredSemaphore, nullptr, &swapchainImageIndex));

    VK_CHECK(vkResetCommandBuffer(mainCommandBuffer, 0));

    VkCommandBuffer cmd = mainCommandBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    VkClearValue clearValue;
    float flash = abs(sin(frameCount / 120.0f));
    clearValue.color = { { 0.0f, 0.0f, flash, 1.0f } };

    VkExtent2D extent;
    extent.height = WINDOW_HEIGHT;
    extent.width = WINDOW_WIDTH;

    VkRenderPassBeginInfo rpInfo = renderpass_begin_info(renderPass, extent, framebuffers[swapchainImageIndex]);
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    switch (currentShader) {
        case 0:
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);
            break;
        case 1:
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rgbTrianglePipeline);
            break;
        default:
            break;
    }
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit = submit_info(&cmd);
    submit.pWaitDstStageMask = &waitStage;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imageAcquiredSemaphore;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderSemaphore;

    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submit, renderFence));

    VkPresentInfoKHR presentInfo = present_info();
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderSemaphore;
    presentInfo.pImageIndices = &swapchainImageIndex;

    VK_CHECK(vkQueuePresentKHR(graphicsQueue, &presentInfo));

    ++frameCount;
}

bool VKlelu::load_shader(const char *path, VkShaderModule &module)
{
    FILE *f = fopen(path, "rb");
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

    vkb::DeviceBuilder deviceBuilder{ vkb_phys };
    auto dev_ret = deviceBuilder.build();
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

    if (!init_pipelines())
        return false;

    return true;
}

bool VKlelu::init_swapchain()
{
    vkb::SwapchainBuilder swapchainBuilder{physicalDevice, device, surface};
    auto swap_ret = swapchainBuilder.use_default_format_selection()
                                    .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                                    .set_desired_extent(WINDOW_WIDTH, WINDOW_HEIGHT)
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

    fprintf(stderr, "Swapchain initialized successfully\n");
    return true;
}

bool VKlelu::init_commands()
{
    VkCommandPoolCreateInfo commandPoolInfo = command_pool_create_info(graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = command_buffer_allocate_info(commandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &mainCommandBuffer));

    fprintf(stderr, "Command pool initialized successfully\n");
    return true;
}

bool VKlelu::init_default_renderpass()
{
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = swapchainImageFormat;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;

    VK_CHECK(vkCreateRenderPass(device, &render_pass_info, nullptr, &renderPass));

    fprintf(stderr, "Renderpass initialized successfully\n");
    return true;
}

bool VKlelu::init_framebuffers()
{
    VkExtent2D extent;
    extent.width = WINDOW_WIDTH;
    extent.height = WINDOW_HEIGHT;
    VkFramebufferCreateInfo fb_info = framebuffer_create_info(renderPass, extent);

    uint32_t swapchainImageCount = swapchainImages.size();
    framebuffers = std::vector<VkFramebuffer>(swapchainImageCount);

    for (int i = 0; i < swapchainImageCount; ++i) {
        fb_info.pAttachments = &swapchainImageViews[i];
        VK_CHECK(vkCreateFramebuffer(device, &fb_info, nullptr, &framebuffers[i]));
    }

    fprintf(stderr, "Framebuffers initialized successfully\n");
    return true;
}

bool VKlelu::init_sync_structures()
{
    VkFenceCreateInfo fenceCreateInfo = fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &renderFence));

    VkSemaphoreCreateInfo semaphoreCreateInfo = semaphore_create_info();
    VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &imageAcquiredSemaphore));
    VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &renderSemaphore));

    fprintf(stderr, "Sync structures initialized successfully\n");
    return true;
}

bool VKlelu::init_pipelines()
{
    VkShaderModule triangleFragShader;
    const char *fragPath = "triangle.frag.spv";
    if (!load_shader(fragPath, triangleFragShader)) {
        fprintf(stderr, "Failed to build fragment shader module %s\n", fragPath);
        return false;
    } else {
        fprintf(stderr, "Fragment shader module %s created successfully\n", fragPath);
    }

    VkShaderModule triangleVertShader;
    const char *vertPath = "triangle.vert.spv";
    if (!load_shader(vertPath, triangleVertShader)) {
        fprintf(stderr, "Failed to build vertex shader module %s\n", vertPath);
        return false;
    } else {
        fprintf(stderr, "Vertex shader module %s created successfully\n", vertPath);
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = pipeline_layout_create_info();
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &trianglePipelineLayout));

    PipelineBuilder builder;
    builder.shaderStages.push_back(pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, triangleVertShader));
    builder.shaderStages.push_back(pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader));
    builder.vertexInputInfo = vertex_input_state_create_info();
    builder.inputAssembly = input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.viewport.x = 0.0f;
    builder.viewport.y = 0.0f;
    builder.viewport.width = WINDOW_WIDTH;
    builder.viewport.height = WINDOW_HEIGHT;
    builder.viewport.minDepth = 0.0f;
    builder.viewport.maxDepth = 1.0f;
    builder.scissor.offset = { 0, 0 };
    builder.scissor.extent = { WINDOW_WIDTH, WINDOW_HEIGHT };
    builder.rasterizer = rasterization_state_create_info(VK_POLYGON_MODE_FILL);
    builder.multisampling = multisampling_state_create_info();
    builder.colorBlendAttachment = color_blend_attachment_state();
    builder.pipelineLayout = trianglePipelineLayout;
    trianglePipeline = builder.build_pipeline(device, renderPass);

    vkDestroyShaderModule(device, triangleVertShader, nullptr);
    vkDestroyShaderModule(device, triangleFragShader, nullptr);

    if (!trianglePipeline) {
        fprintf(stderr, "Failed to create graphics pipeline \"triangle\"\n");
        return false;
    }

    VkShaderModule rgbTriangleFragShader;
    const char *rgbFragPath = "rgb_triangle.frag.spv";
    if (!load_shader(rgbFragPath, rgbTriangleFragShader)) {
        fprintf(stderr, "Failed to build fragment shader module %s\n", rgbFragPath);
        return false;
    } else {
        fprintf(stderr, "Fragment shader module %s created successfully\n", rgbFragPath);
    }

    VkShaderModule rgbTriangleVertShader;
    const char *rgbVertPath = "rgb_triangle.vert.spv";
    if (!load_shader(rgbVertPath, rgbTriangleVertShader)) {
        fprintf(stderr, "Failed to build vertex shader module %s\n", rgbVertPath);
        return false;
    } else {
        fprintf(stderr, "Vertex shader module %s created successfully\n", rgbVertPath);
    }

    builder.shaderStages.clear();
    builder.shaderStages.push_back(pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, rgbTriangleVertShader));
    builder.shaderStages.push_back(pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, rgbTriangleFragShader));
    rgbTrianglePipeline = builder.build_pipeline(device, renderPass);

    vkDestroyShaderModule(device, rgbTriangleVertShader, nullptr);
    vkDestroyShaderModule(device, rgbTriangleFragShader, nullptr);

    if (!rgbTrianglePipeline) {
        fprintf(stderr, "Failed to create graphics pipeline\"rgb_triangle\"\n");
        return false;
    }

    fprintf(stderr, "Graphics pipelines initialized successfully\n");
    return true;
}
