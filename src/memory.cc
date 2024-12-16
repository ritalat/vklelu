#include "memory.hh"

#include "struct_helpers.hh"
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
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = size;
    bufferInfo.usage = usage;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;

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
    VmaAllocatorInfo allocatorInfo = {};
    vmaGetAllocatorInfo(m_allocator, &allocatorInfo);
    m_device = allocatorInfo.device;

    VkImageCreateInfo imgInfo = imageCreateInfo(format, usage, extent);
    imgInfo.samples = samples;

    VmaAllocationCreateInfo imgAllocInfo = {};
    imgAllocInfo.usage = memoryUsage;
    imgAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

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
    VkImageViewCreateInfo viewInfo = imageviewCreateInfo(format, m_image, aspectFlags);
    VkImageView imageView;
    VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &imageView));

    m_imageViews.push_back(imageView);
    return imageView;
}
