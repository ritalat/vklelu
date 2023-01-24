#include "vklelu.hh"

#include "SDL.h"
#include "SDL_vulkan.h"

#include "VkBootstrap.h"

#include "vulkan/vulkan.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768

#define REQUIRED_VK_VERSION_MINOR 3

#define VK_CHECK(x)                                                                       \
    do                                                                                    \
    {                                                                                     \
        VkResult err = x;                                                                 \
        if (err)                                                                          \
        {                                                                                 \
            printf("Detected Vulkan error %d at %s:%d.\n", int(err), __FILE__, __LINE__); \
            abort();                                                                      \
        }                                                                                 \
    } while (0)

VKlelu::VKlelu(int argc, char *argv[]):
    frameCount(0),
    window(nullptr),
    instance(nullptr),
    surface(nullptr),
    device(nullptr),
    swapchain(nullptr)
{
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("Failed to init SDL\n");
        return;
    }

    window = SDL_CreateWindow("VKlelu",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              WINDOW_WIDTH, WINDOW_HEIGHT,
                              SDL_WINDOW_VULKAN);
    if (!window) {
        printf("Failed to create SDL window\n");
        return;
    }

    printf("Window size: %ux%u\n", WINDOW_WIDTH, WINDOW_HEIGHT);
}

VKlelu::~VKlelu()
{
    vkDeviceWaitIdle(device);

    vkDestroySemaphore(device, presentSemaphore, nullptr);
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

    if (!init_swapchain())
        return EXIT_FAILURE;

    if (!init_commands())
        return EXIT_FAILURE;

    if (!init_default_renderpass())
        return EXIT_FAILURE;

    if (!init_framebuffers())
        return EXIT_FAILURE;

    if (!init_sync_structures())
        return EXIT_FAILURE;

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

        VK_CHECK(vkWaitForFences(device, 1, &renderFence, true, 1000000000));
        VK_CHECK(vkResetFences(device, 1, &renderFence));

        uint32_t swapchainImageIndex;
        VK_CHECK(vkAcquireNextImageKHR(device, swapchain, 1000000000, presentSemaphore, nullptr, &swapchainImageIndex));

        VK_CHECK(vkResetCommandBuffer(mainCommandBuffer, 0));

        VkCommandBuffer cmd = mainCommandBuffer;

        VkCommandBufferBeginInfo cmdBeginInfo = {};
        cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBeginInfo.pNext = nullptr;
        cmdBeginInfo.pInheritanceInfo = nullptr;
        cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

        VkClearValue clearValue;
        float flash = abs(sin(frameCount / 120.0f));
        clearValue.color = { { 0.0f, 0.0f, flash, 1.0f } };

        VkExtent2D extent;
        extent.height = WINDOW_HEIGHT;
        extent.width = WINDOW_WIDTH;

        VkRenderPassBeginInfo rpInfo = {};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.pNext = nullptr;
        rpInfo.renderPass = renderPass;
        rpInfo.renderArea.offset.x = 0;
        rpInfo.renderArea.offset.y = 0;
        rpInfo.renderArea.extent = extent;
        rpInfo.framebuffer = framebuffers[swapchainImageIndex];
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdEndRenderPass(cmd);

        VK_CHECK(vkEndCommandBuffer(cmd));

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.pNext = nullptr;
        submit.pWaitDstStageMask = &waitStage;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &presentSemaphore;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &renderSemaphore;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;

        VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submit, renderFence));

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pNext = nullptr;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderSemaphore;
        presentInfo.pImageIndices = &swapchainImageIndex;

        VK_CHECK(vkQueuePresentKHR(graphicsQueue, &presentInfo));

        ++frameCount;
    }

    return EXIT_SUCCESS;
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
        printf("Failed to create Vulkan instance. Error: %s\n", inst_ret.error().message().c_str());
        return false;
    }
    vkb::Instance vkb_inst = inst_ret.value();
    instance = vkb_inst.instance;
    debugMessenger = vkb_inst.debug_messenger;

    if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
        printf("Failed to create Vulkan surface");
        return false;
    }

    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    auto phys_ret = selector.set_surface(surface)
                            .set_minimum_version(1, REQUIRED_VK_VERSION_MINOR)
                            .select();
    if (!phys_ret) {
        printf("Failed to select Vulkan physical device. Error: %s\n", phys_ret.error().message().c_str());
        return false;
    }
    vkb::PhysicalDevice vkb_phys = phys_ret.value();
    physicalDevice = vkb_phys.physical_device;

    vkb::DeviceBuilder deviceBuilder{ vkb_phys };
    auto dev_ret = deviceBuilder.build();
    if (!dev_ret) {
        printf("Failed to create Vulkan device. Error: %s\n", dev_ret.error().message().c_str());
        return false;
    }
    vkb::Device vkb_device = dev_ret.value();
    device = vkb_device.device;

    auto graphics_queue_ret  = vkb_device.get_queue(vkb::QueueType::graphics);
    if (!graphics_queue_ret) {
        printf("Failed to get graphics queue. Error: %s\n", graphics_queue_ret.error().message().c_str());
        return false;
    }
    graphicsQueue = graphics_queue_ret.value();
    graphicsQueueFamily = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

    printf("Vulkan 1.%d initialized successfully\n", REQUIRED_VK_VERSION_MINOR);
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
        printf("Failed to create Vulkan swapchain. Error: %s\n", swap_ret.error().message().c_str());
        return false;
    }
    vkb::Swapchain vkb_swapchain = swap_ret.value();
    swapchain = vkb_swapchain.swapchain;
    swapchainImageFormat = vkb_swapchain.image_format;
    swapchainImages = vkb_swapchain.get_images().value();
    swapchainImageViews = vkb_swapchain.get_image_views().value();

    printf("Vulkan swapchain initialized successfully\n");
    return true;
}

bool VKlelu::init_commands()
{
    VkCommandPoolCreateInfo commandPoolInfo = {};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.pNext = nullptr;
    commandPoolInfo.queueFamilyIndex = graphicsQueueFamily;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.pNext = nullptr;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.commandBufferCount = 1;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &mainCommandBuffer));

    printf("Vulkan command pool initialized successfully\n");
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

    printf("Vulkan renderpass initialized successfully\n");
    return true;
}

bool VKlelu::init_framebuffers()
{
    VkFramebufferCreateInfo fb_info = {};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.pNext = nullptr;
    fb_info.renderPass = renderPass;
    fb_info.attachmentCount = 1;
    fb_info.width = WINDOW_WIDTH;
    fb_info.height = WINDOW_HEIGHT;
    fb_info.layers = 1;

    uint32_t swapchainImageCount = swapchainImages.size();
    framebuffers = std::vector<VkFramebuffer>(swapchainImageCount);

    for (int i = 0; i < swapchainImageCount; ++i) {
        fb_info.pAttachments = &swapchainImageViews[i];
        VK_CHECK(vkCreateFramebuffer(device, &fb_info, nullptr, &framebuffers[i]));
    }

    printf("Vulkan framebuffers initialized successfully\n");
    return true;
}

bool VKlelu::init_sync_structures()
{
    VkFenceCreateInfo fenceCreateInfo = {};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.pNext = nullptr;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &renderFence));

    VkSemaphoreCreateInfo semaphoreCreateInfo = {};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreCreateInfo.pNext = nullptr;
    semaphoreCreateInfo.flags = 0;

    VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &presentSemaphore));
    VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &renderSemaphore));

    printf("Vulkan sync structures initialized successfully\n");
    return true;
}
