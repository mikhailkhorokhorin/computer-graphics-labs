#pragma once

#include <vulkan/vulkan_core.h>

#include "buffer.hpp"

namespace graphics {

struct UniformSet {
	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	VkDescriptorPool pool = VK_NULL_HANDLE;
	VkDescriptorSet set = VK_NULL_HANDLE;
};

bool createUniformSet(const Buffer& buffer, VkDeviceSize size, VkShaderStageFlags stages,
                      UniformSet& uniform_set);
void destroyUniformSet(UniformSet& uniform_set);

}
