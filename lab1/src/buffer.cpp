#include "buffer.hpp"

#include <cstring>
#include <iostream>

#include "graphics_internal.hpp"

namespace graphics {

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer) {
	auto& context = internal::context;

	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
		         VMA_ALLOCATION_CREATE_MAPPED_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VmaAllocationInfo allocation = {};
	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info, &buffer.buffer,
	                    &buffer.allocation, &allocation) != VK_SUCCESS) {
		std::cerr << "Failed to allocate and create Vulkan buffer of " << size << " bytes\n";
		return false;
	}

	buffer.mapped = allocation.pMappedData;
	return true;
}

void writeBuffer(const Buffer& buffer, const void* data, VkDeviceSize size) {
	std::memcpy(buffer.mapped, data, size);
	vmaFlushAllocation(internal::context.allocator, buffer.allocation, 0, size);
}

void destroyBuffer(Buffer& buffer) {
	vmaDestroyBuffer(internal::context.allocator, buffer.buffer, buffer.allocation);
	buffer = {};
}

}
