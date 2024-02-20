#pragma once

#include "SDL.h"
#include "vk_mem_alloc.h"
#include "vulkan/vulkan.h"

#include <cstdint>

struct VulkanContext
{
    VulkanContext(SDL_Window *window);
    ~VulkanContext();
    VulkanContext(const VulkanContext &) = delete;
    VulkanContext &operator=(const VulkanContext &) = delete;

    VkInstance instance;
#if !defined(NDEBUG)
    VkDebugUtilsMessengerEXT debugMessenger;
#endif
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceProperties physicalDeviceProperties;
    VkDevice device;
    VkSurfaceKHR surface;
    VkQueue graphicsQueue;
    uint32_t graphicsQueueFamily;
    VmaAllocator allocator;
};
