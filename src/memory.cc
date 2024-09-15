#include "memory.hh"

#include "struct_helpers.hh"
#include "utils.hh"

#include "vk_mem_alloc.h"
#include "vulkan/vulkan.h"

BufferAllocation::BufferAllocation(VmaAllocator allocator, size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage):
    buffer(VK_NULL_HANDLE),
    allocation(VK_NULL_HANDLE),
    allocator(VK_NULL_HANDLE),
    mapped(false),
    mapping(nullptr)
{
    this->allocator = allocator;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = size;
    bufferInfo.usage = usage;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;

    VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr));
}

BufferAllocation::~BufferAllocation()
{
    if (mapped)
        unmap();
    vmaDestroyBuffer(allocator, buffer, allocation);
}

void *BufferAllocation::map()
{
    if (!mapped) {
        VK_CHECK(vmaMapMemory(allocator, allocation, &mapping));
        mapped = true;
    }
    return mapping;
}

void BufferAllocation::unmap()
{
    if (mapped) {
        vmaUnmapMemory(allocator, allocation);
        mapped = false;
        mapping = nullptr;
    }
}

ImageAllocation::ImageAllocation(VmaAllocator allocator, VkExtent3D extent, VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VmaMemoryUsage memoryUsage):
    image(VK_NULL_HANDLE),
    allocation(VK_NULL_HANDLE),
    allocator(VK_NULL_HANDLE),
    device(VK_NULL_HANDLE)
{
    this->allocator = allocator;

    VmaAllocatorInfo allocatorInfo = {};
    vmaGetAllocatorInfo(this->allocator, &allocatorInfo);
    this->device = allocatorInfo.device;

    VkImageCreateInfo imgInfo = image_create_info(format, usage, extent);
    imgInfo.samples = samples;

    VmaAllocationCreateInfo imgAllocInfo = {};
    imgAllocInfo.usage = memoryUsage;
    imgAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(allocator, &imgInfo, &imgAllocInfo, &image, &allocation, nullptr));
}

ImageAllocation::~ImageAllocation()
{
    if (device) {
        for (VkImageView imageView : imageViews)
            vkDestroyImageView(device, imageView, nullptr);
    }
    vmaDestroyImage(allocator, image, allocation);
}

VkImageView ImageAllocation::create_image_view(VkFormat format, VkImageAspectFlags aspectFlags)
{
    VkImageViewCreateInfo viewInfo = imageview_create_info(format, image, aspectFlags);
    VkImageView imageView;
    VK_CHECK(vkCreateImageView(this->device, &viewInfo, nullptr, &imageView));

    imageViews.push_back(imageView);
    return imageView;
}
