#include "context.hh"

#include "utils.hh"

#include "SDL.h"
#include "SDL_vulkan.h"
#include "vk_mem_alloc.h"
#include "VkBootstrap.h"
#include "vulkan/vulkan.h"

#include <cstdint>
#include <cstdio>
#include <stdexcept>

#define REQUIRED_VK_VERSION_MINOR 3

VulkanContext::VulkanContext(SDL_Window *window):
    instance(VK_NULL_HANDLE),
    device(VK_NULL_HANDLE),
    surface(VK_NULL_HANDLE),
    allocator(VK_NULL_HANDLE)
{
#if !defined(NDEBUG)
    auto sysinfo_ret = vkb::SystemInfo::get_system_info();
    if (!sysinfo_ret) {
        throw std::runtime_error("Failed to gather system info. Error: " + sysinfo_ret.error().message());
    }
    auto sysinfo = sysinfo_ret.value();
    if (sysinfo.validation_layers_available) {
        fprintf(stderr, "Enabling Vulkan validation layers\n");
    } else {
        fprintf(stderr, "Vulkan validation layers not available\n");
    }
#endif

    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("VKlelu")
#if !defined(NDEBUG)
        .request_validation_layers()
        .use_default_debug_messenger()
#endif
        .require_api_version(1, REQUIRED_VK_VERSION_MINOR)
        .build();
    if (!inst_ret) {
        throw std::runtime_error("Failed to create Vulkan instance. Error: " + inst_ret.error().message());
    }

    vkb::Instance vkb_inst = inst_ret.value();
    instance = vkb_inst.instance;
#if !defined(NDEBUG)
    debugMessenger = vkb_inst.debug_messenger;
#endif

    if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
        throw std::runtime_error("Failed to create Vulkan surface");
    }

    VkPhysicalDeviceVulkan13Features required13Features = {};
    required13Features.dynamicRendering = true;
    required13Features.synchronization2 = true;

    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    auto phys_ret = selector.set_surface(surface)
        .set_minimum_version(1, REQUIRED_VK_VERSION_MINOR)
        .set_required_features_13(required13Features)
        .select();
    if (!phys_ret) {
        throw std::runtime_error("Failed to select Vulkan physical device. Error: " + phys_ret.error().message());
    }

    vkb::PhysicalDevice vkb_phys = phys_ret.value();
    physicalDevice = vkb_phys.physical_device;
    physicalDeviceProperties = vkb_phys.properties;

    VkPhysicalDeviceDriverProperties driverProps = {};
    driverProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

    VkPhysicalDeviceProperties2 devProps2 = {};
    devProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    devProps2.pNext = &driverProps;

    vkGetPhysicalDeviceProperties2(physicalDevice, &devProps2);

    vkb::DeviceBuilder deviceBuilder{ vkb_phys };
    auto dev_ret = deviceBuilder.build();
    if (!dev_ret) {
        throw std::runtime_error("Failed to create Vulkan device. Error: " + dev_ret.error().message());
    }

    vkb::Device vkb_device = dev_ret.value();
    device = vkb_device.device;

    auto graphics_queue_ret = vkb_device.get_queue(vkb::QueueType::graphics);
    if (!graphics_queue_ret) {
        throw std::runtime_error("Failed to get graphics queue. Error: " + graphics_queue_ret.error().message());
    }

    graphicsQueue = graphics_queue_ret.value();
    graphicsQueueFamily = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

    fprintf(stderr, "Selected Vulkan device:\n");
    fprintf(stderr, "  Device name:\t%s\n", devProps2.properties.deviceName);
    fprintf(stderr, "  Driver name:\t%s\n", driverProps.driverName);
    fprintf(stderr, "  Driver info:\t%s\n", driverProps.driverInfo);
    fprintf(stderr, "  API version:\t%d.%d.%d\n", VK_API_VERSION_MAJOR(devProps2.properties.apiVersion),
                                                  VK_API_VERSION_MINOR(devProps2.properties.apiVersion),
                                                  VK_API_VERSION_PATCH(devProps2.properties.apiVersion));

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.instance = instance;
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &allocator));

    fprintf(stderr, "Vulkan 1.%d initialized\n", REQUIRED_VK_VERSION_MINOR);
}

VulkanContext::~VulkanContext()
{
    if (allocator)
        vmaDestroyAllocator(allocator);

    if (device)
        vkDestroyDevice(device, nullptr);

    if (surface)
        vkDestroySurfaceKHR(instance, surface, nullptr);

    if (instance) {
#if !defined(NDEBUG)
        vkb::destroy_debug_utils_messenger(instance, debugMessenger);
#endif
        vkDestroyInstance(instance, nullptr);
    }
}
