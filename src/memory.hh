#pragma once

#include "vk_mem_alloc.h"
#include "vulkan/vulkan.h"

#include <vector>

class BufferAllocation
{
public:
    BufferAllocation(VmaAllocator allocator, size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    ~BufferAllocation();
    BufferAllocation(const BufferAllocation &) = delete;
    BufferAllocation &operator=(const BufferAllocation &) = delete;

    VkBuffer buffer();
    void *map();
    void unmap();

private:
    VkBuffer m_buffer;
    VmaAllocation m_allocation;
    VmaAllocator m_allocator;
    bool m_mapped;
    void *m_mapping;
};

class ImageAllocation
{
public:
    ImageAllocation(VmaAllocator allocator, VkExtent3D extent, VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VmaMemoryUsage memoryUsage);
    ~ImageAllocation();
    ImageAllocation(const ImageAllocation &) = delete;
    ImageAllocation &operator=(const ImageAllocation &) = delete;

    VkImage image();
    VkImageView createImageView(VkFormat format, VkImageAspectFlags aspectFlags);

private:
    VkImage m_image;
    VmaAllocation m_allocation;
    VmaAllocator m_allocator;
    VkDevice m_device;
    std::vector<VkImageView> m_imageViews;
};
