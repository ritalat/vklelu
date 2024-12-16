#pragma once

#include "memory.hh"

#include "SDL.h"
#include "vk_mem_alloc.h"
#include "vulkan/vulkan.h"

#include <cstdint>
#include <memory>

class VulkanContext
{
public:
    VulkanContext(SDL_Window *window);
    ~VulkanContext();
    VulkanContext(const VulkanContext &) = delete;
    VulkanContext &operator=(const VulkanContext &) = delete;

    VkInstance instance();
    VkPhysicalDevice physicalDevice();
    VkPhysicalDeviceProperties physicalDeviceProperties();
    VkDevice device();
    VkSurfaceKHR surface();
    VkQueue graphicsQueue();
    uint32_t graphicsQueueFamily();

    std::unique_ptr<BufferAllocation> allocateBuffer(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    std::unique_ptr<ImageAllocation> allocateImage(VkExtent3D extent, VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VmaMemoryUsage memoryUsage);

private:
    VkInstance m_instance;
#if !defined(NDEBUG)
    VkDebugUtilsMessengerEXT m_debugMessenger;
#endif
    VkPhysicalDevice m_physicalDevice;
    VkPhysicalDeviceProperties m_physicalDeviceProperties;
    VkDevice m_device;
    VkSurfaceKHR m_surface;
    VkQueue m_graphicsQueue;
    uint32_t m_graphicsQueueFamily;
    VmaAllocator m_allocator;
};
