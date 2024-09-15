#pragma once

#include "vk_mem_alloc.h"
#include "vulkan/vulkan.h"

#include <vector>

struct BufferAllocation
{
    BufferAllocation(VmaAllocator allocator, size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    ~BufferAllocation();
    BufferAllocation(const BufferAllocation &) = delete;
    BufferAllocation &operator=(const BufferAllocation &) = delete;

    void *map();
    void unmap();

    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocator allocator;
    bool mapped;
    void *mapping;
};

struct ImageAllocation
{
    ImageAllocation(VmaAllocator allocator, VkExtent3D extent, VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VmaMemoryUsage memoryUsage);
    ~ImageAllocation();
    ImageAllocation(const ImageAllocation &) = delete;
    ImageAllocation &operator=(const ImageAllocation &) = delete;

    VkImageView create_image_view(VkFormat format, VkImageAspectFlags aspectFlags);

    VkImage image;
    VmaAllocation allocation;
    VmaAllocator allocator;
    VkDevice device;
    std::vector<VkImageView> imageViews;
};
