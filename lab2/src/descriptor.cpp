#include "descriptor.hpp"

#include <iostream>

#include "graphics_internal.hpp"

namespace graphics {

bool createUniformSet(const Buffer& buffer, VkDeviceSize size, VkShaderStageFlags stages,
                      UniformSet& uniform_set) {
	auto& context = internal::context;

	const VkDescriptorSetLayoutBinding binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = stages,
	};

	const VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};

	if (vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr,
	                                &uniform_set.layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor set layout\n";
		return false;
	}

	const VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
	};

	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};

	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &uniform_set.pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor pool\n";
		return false;
	}

	const VkDescriptorSetAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = uniform_set.pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &uniform_set.layout,
	};

	if (vkAllocateDescriptorSets(context.device, &allocate_info, &uniform_set.set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate Vulkan descriptor set\n";
		return false;
	}

	const VkDescriptorBufferInfo buffer_info = {
		.buffer = buffer.buffer,
		.offset = 0,
		.range = size,
	};

	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = uniform_set.set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &buffer_info,
	};

	vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
	return true;
}

void destroyUniformSet(UniformSet& uniform_set) {
	auto& context = internal::context;
	vkDestroyDescriptorPool(context.device, uniform_set.pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, uniform_set.layout, nullptr);
	uniform_set = {};
}

}
