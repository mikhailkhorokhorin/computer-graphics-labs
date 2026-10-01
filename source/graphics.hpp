#pragma once

#include <vulkan/vulkan_core.h>

#include <vk_mem_alloc.h>

namespace graphics {

struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* mapped = nullptr;
};

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer);
void writeBuffer(const Buffer& buffer, const void* data, VkDeviceSize size);
void destroyBuffer(Buffer& buffer);

VkShaderModule loadShaderModule(const char* path);

}
