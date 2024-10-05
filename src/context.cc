#include "context.hh"

#include "memory.hh"
#include "utils.hh"

#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"
#include "vk_mem_alloc.h"
#include "VkBootstrap.h"
#include "vulkan/vulkan.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>

#define REQUIRED_VK_VERSION_MINOR 3

VulkanContext::VulkanContext(SDL_Window *window):
    m_instance(VK_NULL_HANDLE),
    m_device(VK_NULL_HANDLE),
    m_surface(VK_NULL_HANDLE),
    m_allocator(VK_NULL_HANDLE)
{
#if !defined(NDEBUG)
    auto sysinfoRet = vkb::SystemInfo::get_system_info();
    if (!sysinfoRet) {
        throw std::runtime_error("Failed to gather system info. Error: " + sysinfoRet.error().message());
    }
    auto sysinfo = sysinfoRet.value();
    if (sysinfo.validation_layers_available) {
        fprintf(stderr, "Enabling Vulkan validation layers\n");
    } else {
        fprintf(stderr, "Vulkan validation layers not available\n");
    }
#endif

    vkb::InstanceBuilder builder;
    auto instRet = builder.set_app_name("VKlelu")
#if !defined(NDEBUG)
        .request_validation_layers()
        .use_default_debug_messenger()
#endif
        .require_api_version(1, REQUIRED_VK_VERSION_MINOR)
        .build();
    if (!instRet) {
        throw std::runtime_error("Failed to create Vulkan instance. Error: " + instRet.error().message());
    }

    vkb::Instance vkbInst = instRet.value();
    m_instance = vkbInst.instance;
#if !defined(NDEBUG)
    m_debugMessenger = vkbInst.debug_messenger;
#endif

    if (!SDL_Vulkan_CreateSurface(window, m_instance, NULL, &m_surface)) {
        throw std::runtime_error("Failed to create Vulkan surface");
    }

    VkPhysicalDeviceVulkan13Features required13Features = {};
    required13Features.dynamicRendering = true;
    required13Features.synchronization2 = true;

    vkb::PhysicalDeviceSelector selector{ vkbInst };
    auto physRet = selector.set_surface(m_surface)
        .set_minimum_version(1, REQUIRED_VK_VERSION_MINOR)
        .set_required_features_13(required13Features)
        .select();
    if (!physRet) {
        throw std::runtime_error("Failed to select Vulkan physical device. Error: " + physRet.error().message());
    }

    vkb::PhysicalDevice vkbPhys = physRet.value();
    m_physicalDevice = vkbPhys.physical_device;
    m_physicalDeviceProperties = vkbPhys.properties;

    VkPhysicalDeviceDriverProperties driverProps = {};
    driverProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

    VkPhysicalDeviceProperties2 devProps2 = {};
    devProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    devProps2.pNext = &driverProps;

    vkGetPhysicalDeviceProperties2(m_physicalDevice, &devProps2);

    vkb::DeviceBuilder deviceBuilder{ vkbPhys };
    auto devRet = deviceBuilder.build();
    if (!devRet) {
        throw std::runtime_error("Failed to create Vulkan device. Error: " + devRet.error().message());
    }

    vkb::Device vkbDev = devRet.value();
    m_device = vkbDev.device;

    auto graphicsQueueRet = vkbDev.get_queue(vkb::QueueType::graphics);
    if (!graphicsQueueRet) {
        throw std::runtime_error("Failed to get graphics queue. Error: " + graphicsQueueRet.error().message());
    }

    m_graphicsQueue = graphicsQueueRet.value();
    m_graphicsQueueFamily = vkbDev.get_queue_index(vkb::QueueType::graphics).value();

    fprintf(stderr, "Selected Vulkan device:\n");
    fprintf(stderr, "  Device name:\t%s\n", devProps2.properties.deviceName);
    fprintf(stderr, "  Driver name:\t%s\n", driverProps.driverName);
    fprintf(stderr, "  Driver info:\t%s\n", driverProps.driverInfo);
    fprintf(stderr, "  API version:\t%d.%d.%d\n", VK_API_VERSION_MAJOR(devProps2.properties.apiVersion),
                                                  VK_API_VERSION_MINOR(devProps2.properties.apiVersion),
                                                  VK_API_VERSION_PATCH(devProps2.properties.apiVersion));

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.instance = m_instance;
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_allocator));

    fprintf(stderr, "Vulkan 1.%d initialized\n", REQUIRED_VK_VERSION_MINOR);
}

VulkanContext::~VulkanContext()
{
    if (m_allocator)
        vmaDestroyAllocator(m_allocator);

    if (m_device)
        vkDestroyDevice(m_device, nullptr);

    if (m_surface)
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

    if (m_instance) {
#if !defined(NDEBUG)
        vkb::destroy_debug_utils_messenger(m_instance, m_debugMessenger);
#endif
        vkDestroyInstance(m_instance, nullptr);
    }
}

VkInstance VulkanContext::instance()
{
    return m_instance;
}

VkPhysicalDevice VulkanContext::physicalDevice()
{
    return m_physicalDevice;
}

VkPhysicalDeviceProperties VulkanContext::physicalDeviceProperties()
{
    return m_physicalDeviceProperties;
}

VkDevice VulkanContext::device()
{
    return m_device;
}

VkSurfaceKHR VulkanContext::surface()
{
    return m_surface;
}

VkQueue VulkanContext::graphicsQueue()
{
    return m_graphicsQueue;
}

uint32_t VulkanContext::graphicsQueueFamily()
{
    return m_graphicsQueueFamily;
}

std::unique_ptr<BufferAllocation> VulkanContext::allocateBuffer(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
    return std::make_unique<BufferAllocation>(m_allocator, size, usage, memoryUsage);
}

std::unique_ptr<ImageAllocation> VulkanContext::allocateImage(VkExtent3D extent, VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VmaMemoryUsage memoryUsage)
{
    return std::make_unique<ImageAllocation>(m_allocator, extent, format, samples, usage, memoryUsage);
}
