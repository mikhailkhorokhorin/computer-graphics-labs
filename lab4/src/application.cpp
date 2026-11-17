#include "application.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

#include <imgui.h>
#include <lodepng.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>

namespace application {

namespace {

constexpr uint32_t object_count = 2;
constexpr uint32_t shadow_size = 2048;
constexpr VkFormat shadow_format = VK_FORMAT_D16_UNORM;
constexpr float floor_height = -1.3f;
constexpr float floor_half_size = 5.0f;

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 uv;
};

struct Uniforms {
	glm::mat4 model;
	glm::mat4 view_projection;
	glm::mat4 light_view_projection;
	glm::mat4 normal_matrix;
	glm::vec4 camera_position;
	glm::vec4 light_direction;
	glm::vec4 light_color;
	glm::vec4 ambient_color;
	glm::vec4 specular;
	glm::vec4 albedo;
};

struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* mapped = nullptr;
};

struct Draw {
	uint32_t first_index;
	uint32_t index_count;
};

Buffer vertex_buffer, index_buffer, uniform_buffer;
VkDeviceSize uniform_stride = 0;
Draw draws[object_count];

VkImage texture_image;
VmaAllocation texture_allocation;
VkImageView texture_view;
VkSampler sampler;

VkImage shadow_image;
VmaAllocation shadow_allocation;
VkImageView shadow_view;
VkSampler shadow_sampler;

VkDescriptorSetLayout set_layout;
VkDescriptorPool descriptor_pool;
VkDescriptorSet descriptor_set;
VkPipelineLayout pipeline_layout;
VkPipeline pipeline;
VkPipeline shadow_pipeline;

glm::vec3 camera_position(0.0f, 3.0f, 6.5f);
float camera_yaw = 0.0f;
float camera_pitch = glm::radians(-28.0f);
float camera_fov = glm::radians(60.0f);

float light_azimuth = glm::radians(50.0f);
float light_elevation = glm::radians(40.0f);
glm::vec3 light_color(1.0f);
float light_intensity = 1.0f;

glm::vec3 ambient_color(1.0f);
float ambient_intensity = 0.2f;

glm::vec3 specular_color(0.5f);
float shininess = 32.0f;
glm::vec3 floor_color(0.75f);

float shadow_area = 5.0f;
float depth_bias = 2.0f;
float slope_bias = 2.0f;

float rotation_speed = 0.3f;
float rotation_angle = 0.0f;
double previous_time = -1.0;
Uniforms uniforms[object_count];

void buildMesh(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices) {
	const glm::vec3 corners[4] = {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}};

	auto cut = [&](int i, int j) { return glm::normalize(2.0f * corners[i] + corners[j]); };

	auto addFace = [&](std::vector<glm::vec3> points) {
		glm::vec3 center(0.0f);
		for (auto& p : points) center += p;
		center /= float(points.size());
		glm::vec3 normal = glm::normalize(glm::cross(points[1] - points[0], points[2] - points[0]));
		if (glm::dot(normal, center) < 0.0f) {
			std::reverse(points.begin(), points.end());
			normal = -normal;
		}

		const glm::vec3 u_axis = glm::normalize(points[1] - points[0]);
		const glm::vec3 v_axis = glm::cross(u_axis, normal);
		const auto first = uint16_t(vertices.size());
		for (auto& p : points) {
			const glm::vec3 d = p - center;
			vertices.push_back({p, normal, 0.5f + 0.55f * glm::vec2(glm::dot(d, u_axis), glm::dot(d, v_axis))});
		}
		for (size_t i = 1; i + 1 < points.size(); ++i) {
			indices.insert(indices.end(), {first, uint16_t(first + i), uint16_t(first + i + 1)});
		}
	};

	for (int i = 0; i < 4; ++i) {
		const int a = (i + 1) % 4, b = (i + 2) % 4, c = (i + 3) % 4;
		addFace({cut(i, a), cut(i, b), cut(i, c)});
		addFace({cut(a, b), cut(b, a), cut(b, c), cut(c, b), cut(c, a), cut(a, c)});
	}
	draws[0] = {0, uint32_t(indices.size())};

	const float h = floor_half_size;
	const glm::vec3 up(0.0f, 1.0f, 0.0f);
	const auto first = uint16_t(vertices.size());
	vertices.push_back({{-h, floor_height, -h}, up, {0.0f, 0.0f}});
	vertices.push_back({{-h, floor_height, h}, up, {0.0f, 1.0f}});
	vertices.push_back({{h, floor_height, h}, up, {1.0f, 1.0f}});
	vertices.push_back({{h, floor_height, -h}, up, {1.0f, 0.0f}});
	draws[1] = {uint32_t(indices.size()), 6};
	indices.insert(indices.end(), {first, uint16_t(first + 1), uint16_t(first + 2)});
	indices.insert(indices.end(), {first, uint16_t(first + 2), uint16_t(first + 3)});
}

bool createResources(const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices) {
	auto& context = graphics::internal::context;

	auto createBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage, bool host, Buffer& b) {
		const VkBufferCreateInfo info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = size,
			.usage = usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};
		VmaAllocationCreateInfo alloc = {.usage = VMA_MEMORY_USAGE_AUTO};
		if (host) {
			alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}
		VmaAllocationInfo result{};
		if (vmaCreateBuffer(context.allocator, &info, &alloc, &b.buffer, &b.allocation, &result) != VK_SUCCESS) {
			return false;
		}
		b.mapped = result.pMappedData;
		return true;
	};

	auto createImage = [&](VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage, VkImage& image,
		VmaAllocation& allocation) {
		const VkImageCreateInfo info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = format,
			.extent = {width, height, 1},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};
		const VmaAllocationCreateInfo alloc = {.usage = VMA_MEMORY_USAGE_AUTO};
		return vmaCreateImage(context.allocator, &info, &alloc, &image, &allocation, nullptr) == VK_SUCCESS;
	};

	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(context.physical_device, &properties);
	const VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
	uniform_stride = (sizeof(Uniforms) + alignment - 1) / alignment * alignment;

	std::vector<unsigned char> pixels;
	unsigned width = 0, height = 0;
	if (lodepng::decode(pixels, width, height, TEXTURE_DIR "cat.png") != 0) return false;

	const VkDeviceSize pixels_size = pixels.size();
	const VkDeviceSize vertices_size = vertices.size() * sizeof(Vertex);
	const VkDeviceSize indices_size = indices.size() * sizeof(uint16_t);
	const VkBufferUsageFlags dst = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	Buffer staging;
	bool ok = createBuffer(uniform_stride * object_count, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, uniform_buffer);
	ok = ok && createBuffer(vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | dst, false, vertex_buffer);
	ok = ok && createBuffer(indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | dst, false, index_buffer);
	ok = ok && createBuffer(pixels_size + vertices_size + indices_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, staging);
	ok = ok && createImage(VK_FORMAT_R8G8B8A8_SRGB, width, height,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, texture_image, texture_allocation);
	ok = ok && createImage(shadow_format, shadow_size, shadow_size,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, shadow_image, shadow_allocation);
	if (!ok) return false;

	auto* staging_data = static_cast<char*>(staging.mapped);
	std::memcpy(staging_data, pixels.data(), pixels_size);
	std::memcpy(staging_data + pixels_size, vertices.data(), vertices_size);
	std::memcpy(staging_data + pixels_size + vertices_size, indices.data(), indices_size);
	vmaFlushAllocation(context.allocator, staging.allocation, 0, VK_WHOLE_SIZE);

	const VkCommandPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = context.graphics_queue_index,
	};
	VkCommandPool command_pool;
	if (vkCreateCommandPool(context.device, &pool_info, nullptr, &command_pool) != VK_SUCCESS) return false;

	const VkCommandBufferAllocateInfo cmd_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VkCommandBuffer cmd;
	vkAllocateCommandBuffers(context.device, &cmd_info, &cmd);

	const VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd, &begin);

	const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	VkImageMemoryBarrier image_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = texture_image,
		.subresourceRange = range,
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
		0, nullptr, 0, nullptr, 1, &image_barrier);

	const VkBufferImageCopy image_copy = {
		.bufferOffset = 0,
		.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
		.imageExtent = {width, height, 1},
	};
	const VkBufferCopy vertex_copy = {pixels_size, 0, vertices_size};
	const VkBufferCopy index_copy = {pixels_size + vertices_size, 0, indices_size};
	vkCmdCopyBufferToImage(cmd, staging.buffer, texture_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_copy);
	vkCmdCopyBuffer(cmd, staging.buffer, vertex_buffer.buffer, 1, &vertex_copy);
	vkCmdCopyBuffer(cmd, staging.buffer, index_buffer.buffer, 1, &index_copy);

	image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	image_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	image_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	const VkMemoryBarrier buffers_barrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT,
	};
	const VkPipelineStageFlags dst_stages = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, dst_stages, 0,
		1, &buffers_barrier, 0, nullptr, 1, &image_barrier);
	vkEndCommandBuffer(cmd);

	const VkSubmitInfo submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd,
	};
	vkQueueSubmit(context.graphics_queue, 1, &submit, VK_NULL_HANDLE);
	vkQueueWaitIdle(context.graphics_queue);
	vkDestroyCommandPool(context.device, command_pool, nullptr);
	vmaDestroyBuffer(context.allocator, staging.buffer, staging.allocation);

	const VkImageViewCreateInfo view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = texture_image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_SRGB,
		.subresourceRange = range,
	};
	const VkImageViewCreateInfo shadow_view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = shadow_image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = shadow_format,
		.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
	};
	const VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
	};
	const VkSamplerCreateInfo shadow_sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.compareEnable = VK_TRUE,
		.compareOp = VK_COMPARE_OP_LESS,
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
	};
	ok = vkCreateImageView(context.device, &view_info, nullptr, &texture_view) == VK_SUCCESS;
	ok = ok && vkCreateImageView(context.device, &shadow_view_info, nullptr, &shadow_view) == VK_SUCCESS;
	ok = ok && vkCreateSampler(context.device, &sampler_info, nullptr, &sampler) == VK_SUCCESS;
	return ok && vkCreateSampler(context.device, &shadow_sampler_info, nullptr, &shadow_sampler) == VK_SUCCESS;
}

}

bool initialize() {
	auto& context = graphics::internal::context;

	std::vector<Vertex> vertices;
	std::vector<uint16_t> indices;
	buildMesh(vertices, indices);

	ImGui::GetStyle().FontScaleMain = 1.8f;
	ImGui::GetStyle().ScaleAllSizes(1.8f);

	if (!createResources(vertices, indices)) return false;

	const VkDescriptorSetLayoutBinding set_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		{
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
	};
	const VkDescriptorSetLayoutCreateInfo set_layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 3,
		.pBindings = set_bindings,
	};
	if (vkCreateDescriptorSetLayout(context.device, &set_layout_info, nullptr, &set_layout) != VK_SUCCESS) {
		return false;
	}

	const VkDescriptorPoolSize pool_sizes[] = {
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
	};
	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 2,
		.pPoolSizes = pool_sizes,
	};
	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
		return false;
	}

	const VkDescriptorSetAllocateInfo set_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &set_layout,
	};
	if (vkAllocateDescriptorSets(context.device, &set_alloc_info, &descriptor_set) != VK_SUCCESS) {
		return false;
	}

	const VkDescriptorBufferInfo buffer_info = {uniform_buffer.buffer, 0, sizeof(Uniforms)};
	const VkDescriptorImageInfo image_info = {sampler, texture_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	const VkDescriptorImageInfo shadow_info = {shadow_sampler, shadow_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	const VkWriteDescriptorSet writes[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.pBufferInfo = &buffer_info,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_set,
			.dstBinding = 1,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &image_info,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_set,
			.dstBinding = 2,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &shadow_info,
		},
	};
	vkUpdateDescriptorSets(context.device, 3, writes, 0, nullptr);

	const VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &set_layout,
	};
	if (vkCreatePipelineLayout(context.device, &pipeline_layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
		return false;
	}

	auto loadShader = [&](const char* path) {
		VkShaderModule module = VK_NULL_HANDLE;
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file) return module;
		std::vector<uint32_t> code(size_t(file.tellg()) / 4);
		file.seekg(0);
		file.read(reinterpret_cast<char*>(code.data()), std::streamsize(code.size() * 4));
		const VkShaderModuleCreateInfo info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = code.size() * 4,
			.pCode = code.data(),
		};
		vkCreateShaderModule(context.device, &info, nullptr, &module);
		return module;
	};

	const VkShaderModule vert = loadShader(SHADER_DIR "mesh.vert.spv");
	const VkShaderModule frag = loadShader(SHADER_DIR "mesh.frag.spv");
	const VkShaderModule shadow_vert = loadShader(SHADER_DIR "shadow.vert.spv");
	const VkPipelineShaderStageCreateInfo stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vert,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = frag,
			.pName = "main",
		},
	};
	const VkPipelineShaderStageCreateInfo shadow_stage = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.module = shadow_vert,
		.pName = "main",
	};

	const VkVertexInputBindingDescription vertex_binding = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
	const VkVertexInputAttributeDescription attributes[] = {
		{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
		{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
		{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
	};
	const VkPipelineVertexInputStateCreateInfo input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vertex_binding,
		.vertexAttributeDescriptionCount = 3,
		.pVertexAttributeDescriptions = attributes,
	};
	const VkPipelineVertexInputStateCreateInfo shadow_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vertex_binding,
		.vertexAttributeDescriptionCount = 1,
		.pVertexAttributeDescriptions = attributes,
	};
	const VkPipelineInputAssemblyStateCreateInfo assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};
	const VkPipelineViewportStateCreateInfo viewport = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};
	const VkPipelineRasterizationStateCreateInfo raster = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};
	const VkPipelineRasterizationStateCreateInfo shadow_raster = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.depthBiasEnable = VK_TRUE,
		.lineWidth = 1.0f,
	};
	const VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};
	const VkPipelineDepthStencilStateCreateInfo depth = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};
	const VkPipelineColorBlendAttachmentState blend_attachment = {.colorWriteMask = 0xF};
	const VkPipelineColorBlendStateCreateInfo blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blend_attachment,
	};
	const VkPipelineColorBlendStateCreateInfo shadow_blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
	};
	const VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_DEPTH_BIAS,
	};
	const VkPipelineDynamicStateCreateInfo dynamic = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamic_states,
	};
	const VkPipelineDynamicStateCreateInfo shadow_dynamic = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 3,
		.pDynamicStates = dynamic_states,
	};
	const VkPipelineRenderingCreateInfo shadow_rendering = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.depthAttachmentFormat = shadow_format,
	};

	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = stages,
		.pVertexInputState = &input,
		.pInputAssemblyState = &assembly,
		.pViewportState = &viewport,
		.pRasterizationState = &raster,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth,
		.pColorBlendState = &blend,
		.pDynamicState = &dynamic,
		.layout = pipeline_layout,
		.renderPass = context.render_pass,
	};
	const VkGraphicsPipelineCreateInfo shadow_pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &shadow_rendering,
		.stageCount = 1,
		.pStages = &shadow_stage,
		.pVertexInputState = &shadow_input,
		.pInputAssemblyState = &assembly,
		.pViewportState = &viewport,
		.pRasterizationState = &shadow_raster,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth,
		.pColorBlendState = &shadow_blend,
		.pDynamicState = &shadow_dynamic,
		.layout = pipeline_layout,
	};
	VkResult result = vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
	VkResult shadow_result =
		vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &shadow_pipeline_info, nullptr, &shadow_pipeline);
	vkDestroyShaderModule(context.device, vert, nullptr);
	vkDestroyShaderModule(context.device, frag, nullptr);
	vkDestroyShaderModule(context.device, shadow_vert, nullptr);
	return vert && frag && shadow_vert && result == VK_SUCCESS && shadow_result == VK_SUCCESS;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);
	vkDestroyPipeline(context.device, shadow_pipeline, nullptr);
	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, set_layout, nullptr);
	vkDestroySampler(context.device, shadow_sampler, nullptr);
	vkDestroySampler(context.device, sampler, nullptr);
	vkDestroyImageView(context.device, shadow_view, nullptr);
	vkDestroyImageView(context.device, texture_view, nullptr);
	vmaDestroyImage(context.allocator, shadow_image, shadow_allocation);
	vmaDestroyImage(context.allocator, texture_image, texture_allocation);
	vmaDestroyBuffer(context.allocator, uniform_buffer.buffer, uniform_buffer.allocation);
	vmaDestroyBuffer(context.allocator, index_buffer.buffer, index_buffer.allocation);
	vmaDestroyBuffer(context.allocator, vertex_buffer.buffer, vertex_buffer.allocation);
}

void update(double time) {
	const float dt = previous_time < 0 ? 0.0f : std::min(float(time - previous_time), 0.1f);
	previous_time = time;

	const ImGuiSliderFlags clamp = ImGuiSliderFlags_AlwaysClamp;
	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
	ImGui::Begin("Lab 4", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::Text("Truncated tetrahedron, %.1f FPS", double(ImGui::GetIO().Framerate));
	if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::DragFloat3("Position", &camera_position.x, 0.01f);
		ImGui::SliderAngle("Yaw", &camera_yaw, -180.0f, 180.0f);
		ImGui::SliderAngle("Pitch", &camera_pitch, -89.0f, 89.0f, "%.0f deg", clamp);
		ImGui::SliderAngle("Field of view", &camera_fov, 20.0f, 120.0f, "%.0f deg", clamp);
	}
	if (ImGui::CollapsingHeader("Directional light", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderAngle("Azimuth", &light_azimuth, -180.0f, 180.0f);
		ImGui::SliderAngle("Elevation", &light_elevation, 10.0f, 89.0f, "%.0f deg", clamp);
		ImGui::ColorEdit3("Light color", &light_color.x);
		ImGui::SliderFloat("Intensity", &light_intensity, 0.0f, 5.0f, "%.2f", clamp);
	}
	if (ImGui::CollapsingHeader("Ambient light", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::ColorEdit3("Ambient color", &ambient_color.x);
		ImGui::SliderFloat("Ambient intensity", &ambient_intensity, 0.0f, 1.0f, "%.2f", clamp);
	}
	if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::ColorEdit3("Specular color", &specular_color.x);
		ImGui::SliderFloat("Shininess", &shininess, 1.0f, 256.0f, "%.0f", clamp | ImGuiSliderFlags_Logarithmic);
		ImGui::ColorEdit3("Floor color", &floor_color.x);
	}
	if (ImGui::CollapsingHeader("Shadow", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Area", &shadow_area, 1.0f, 10.0f, "%.1f", clamp);
		ImGui::SliderFloat("Depth bias", &depth_bias, 0.0f, 10.0f, "%.2f", clamp);
		ImGui::SliderFloat("Slope bias", &slope_bias, 0.0f, 10.0f, "%.2f", clamp);
	}
	ImGui::SliderFloat("Rotation speed", &rotation_speed, 0.0f, 3.0f, "%.2f rad/s", clamp);
	ImGui::End();

	rotation_angle += rotation_speed * dt;

	const auto extent = graphics::internal::context.swapchain_extent;
	const float aspect = float(extent.width) / float(extent.height);
	const glm::vec3 axis = glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f));
	const glm::vec3 up(0.0f, 1.0f, 0.0f);
	const glm::vec3 forward = {
		std::cos(camera_pitch) * std::sin(camera_yaw),
		std::sin(camera_pitch),
		-std::cos(camera_pitch) * std::cos(camera_yaw),
	};
	const glm::vec3 to_light = {
		std::cos(light_elevation) * std::sin(light_azimuth),
		std::sin(light_elevation),
		std::cos(light_elevation) * std::cos(light_azimuth),
	};

	const glm::mat4 view = glm::lookAt(camera_position, camera_position + forward, up);
	glm::mat4 projection = glm::perspectiveRH_ZO(camera_fov, aspect, 0.1f, 100.0f);
	projection[1][1] *= -1.0f;

	const glm::mat4 light_view = glm::lookAt(10.0f * to_light, glm::vec3(0.0f), up);
	const glm::mat4 light_projection =
		glm::orthoRH_ZO(-shadow_area, shadow_area, -shadow_area, shadow_area, 1.0f, 20.0f);

	const glm::mat4 models[object_count] = {
		glm::rotate(glm::mat4(1.0f), rotation_angle, axis),
		glm::mat4(1.0f),
	};
	const glm::vec4 albedos[object_count] = {
		glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
		glm::vec4(floor_color, 0.0f),
	};
	for (uint32_t i = 0; i < object_count; ++i) {
		uniforms[i] = {
			.model = models[i],
			.view_projection = projection * view,
			.light_view_projection = light_projection * light_view,
			.normal_matrix = glm::mat4(glm::transpose(glm::inverse(glm::mat3(models[i])))),
			.camera_position = glm::vec4(camera_position, 1.0f),
			.light_direction = glm::vec4(-to_light, 0.0f),
			.light_color = glm::vec4(light_color, light_intensity),
			.ambient_color = glm::vec4(ambient_color, ambient_intensity),
			.specular = glm::vec4(specular_color, shininess),
			.albedo = albedos[i],
		};
	}
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;
	const VkCommandBuffer cmd = fd.command_buffer;
	if (cmd == VK_NULL_HANDLE) return;

	for (uint32_t i = 0; i < object_count; ++i) {
		std::memcpy(static_cast<char*>(uniform_buffer.mapped) + i * uniform_stride, &uniforms[i], sizeof(Uniforms));
	}
	vmaFlushAllocation(context.allocator, uniform_buffer.allocation, 0, VK_WHOLE_SIZE);

	vkResetCommandBuffer(cmd, 0);
	const VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd, &begin);

	auto drawScene = [&](VkPipeline scene_pipeline) {
		const VkDeviceSize offset = 0;
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_pipeline);
		vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer.buffer, &offset);
		vkCmdBindIndexBuffer(cmd, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
		for (uint32_t i = 0; i < object_count; ++i) {
			const auto dynamic_offset = uint32_t(i * uniform_stride);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set, 1, &dynamic_offset);
			vkCmdDrawIndexed(cmd, draws[i].index_count, 1, draws[i].first_index, 0, 0);
		}
	};

	VkImageMemoryBarrier shadow_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = shadow_image,
		.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
	};
	const VkPipelineStageFlags depth_stages =
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, depth_stages, 0,
		0, nullptr, 0, nullptr, 1, &shadow_barrier);

	const VkRenderingAttachmentInfo shadow_attachment = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = shadow_view,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = {.depthStencil = {1.0f, 0}},
	};
	const VkRenderingInfo shadow_rendering = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = {{0, 0}, {shadow_size, shadow_size}},
		.layerCount = 1,
		.pDepthAttachment = &shadow_attachment,
	};
	vkCmdBeginRendering(cmd, &shadow_rendering);
	const VkViewport shadow_viewport = {0, 0, float(shadow_size), float(shadow_size), 0.0f, 1.0f};
	const VkRect2D shadow_scissor = {{0, 0}, {shadow_size, shadow_size}};
	vkCmdSetViewport(cmd, 0, 1, &shadow_viewport);
	vkCmdSetScissor(cmd, 0, 1, &shadow_scissor);
	vkCmdSetDepthBias(cmd, depth_bias, 0.0f, slope_bias);
	drawScene(shadow_pipeline);
	vkCmdEndRendering(cmd);

	shadow_barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	shadow_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	shadow_barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	shadow_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		0, nullptr, 0, nullptr, 1, &shadow_barrier);

	const VkClearValue clear[] = {
		{.color = {{0.08f, 0.08f, 0.1f, 1.0f}}},
		{.depthStencil = {1.0f, 0}},
	};
	const VkRenderPassBeginInfo pass = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = {.extent = context.swapchain_extent},
		.clearValueCount = 2,
		.pClearValues = clear,
	};
	vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);

	const VkExtent2D extent = context.swapchain_extent;
	const VkViewport viewport = {0, 0, float(extent.width), float(extent.height), 0.0f, 1.0f};
	const VkRect2D scissor = {{0, 0}, extent};
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	drawScene(pipeline);

	vkCmdEndRenderPass(cmd);
	vkEndCommandBuffer(cmd);
}

}
