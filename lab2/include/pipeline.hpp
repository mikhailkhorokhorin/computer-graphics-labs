#pragma once

#include <vulkan/vulkan_core.h>

namespace graphics {

struct Pipeline {
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
};

bool createMeshPipeline(const char* vertex_path, const char* fragment_path,
                        VkDescriptorSetLayout set_layout, Pipeline& pipeline);
void destroyPipeline(Pipeline& pipeline);

}
