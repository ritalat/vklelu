#include "memory.hh"

#include "utils.hh"

#include "vk_mem_alloc.h"
#include "vulkan/vulkan.h"

BufferAllocation::BufferAllocation(VmaAllocator allocator, size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage):
    m_buffer(VK_NULL_HANDLE),
    m_allocation(VK_NULL_HANDLE),
    m_allocator(allocator),
    m_mapped(false),
    m_mapping(nullptr)
{
    VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage
    };

    VmaAllocationCreateInfo allocInfo {
        .usage = memoryUsage
    };

    VK_CHECK(vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &m_buffer, &m_allocation, nullptr));
}

BufferAllocation::~BufferAllocation()
{
    if (m_mapped)
        unmap();
    vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
}

VkBuffer BufferAllocation::buffer()
{
    return m_buffer;
}

void *BufferAllocation::map()
{
    if (!m_mapped) {
        VK_CHECK(vmaMapMemory(m_allocator, m_allocation, &m_mapping));
        m_mapped = true;
    }
    return m_mapping;
}

void BufferAllocation::unmap()
{
    if (m_mapped) {
        vmaUnmapMemory(m_allocator, m_allocation);
        m_mapped = false;
        m_mapping = nullptr;
    }
}

ImageAllocation::ImageAllocation(VmaAllocator allocator, VkExtent3D extent, VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VmaMemoryUsage memoryUsage):
    m_image(VK_NULL_HANDLE),
    m_allocation(VK_NULL_HANDLE),
    m_allocator(allocator),
    m_device(VK_NULL_HANDLE)
{
    VmaAllocatorInfo allocatorInfo {};
    vmaGetAllocatorInfo(m_allocator, &allocatorInfo);
    m_device = allocatorInfo.device;

    VkImageCreateInfo imgInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = extent,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = samples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage
    };

    VmaAllocationCreateInfo imgAllocInfo {
        .usage = memoryUsage,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    };

    VK_CHECK(vmaCreateImage(m_allocator, &imgInfo, &imgAllocInfo, &m_image, &m_allocation, nullptr));
}

ImageAllocation::~ImageAllocation()
{
    if (m_device) {
        for (VkImageView imageView : m_imageViews)
            vkDestroyImageView(m_device, imageView, nullptr);
    }
    vmaDestroyImage(m_allocator, m_image, m_allocation);
}

VkImage ImageAllocation::image()
{
    return m_image;
}

VkImageView ImageAllocation::createImageView(VkFormat format, VkImageAspectFlags aspectFlags)
{
    VkImageSubresourceRange range {
        .aspectMask = aspectFlags,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
    };

    VkImageViewCreateInfo viewInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = range
    };

    VkImageView imageView;
    VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &imageView));

    m_imageViews.push_back(imageView);
    return imageView;
}
