#include "shader.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

#include "graphics_internal.hpp"

namespace graphics {

VkShaderModule loadShaderModule(const char* path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		std::cerr << "Failed to open shader file " << path << '\n';
		return VK_NULL_HANDLE;
	}

	const std::streamsize size = file.tellg();
	std::vector<uint32_t> code(size_t(size) / sizeof(uint32_t));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(code.data()), size);

	const VkShaderModuleCreateInfo shader_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = code.size() * sizeof(uint32_t),
		.pCode = code.data(),
	};

	VkShaderModule shader = VK_NULL_HANDLE;
	if (vkCreateShaderModule(internal::context.device, &shader_info, nullptr, &shader) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan shader module from " << path << '\n';
		return VK_NULL_HANDLE;
	}
	return shader;
}

}
