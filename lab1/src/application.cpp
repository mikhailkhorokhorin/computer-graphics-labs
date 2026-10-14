#include "application.hpp"

#include <algorithm>

#include <imgui.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>

#include "buffer.hpp"
#include "descriptor.hpp"
#include "pipeline.hpp"
#include "mesh.hpp"

namespace application {

namespace {

constexpr float max_delta_time = 0.1f;

struct Uniforms {
	glm::mat4 mvp;
};

uint32_t index_count;
graphics::Buffer vertex_buffer;
graphics::Buffer index_buffer;
graphics::Buffer uniform_buffer;
graphics::UniformSet uniform_set;
graphics::Pipeline pipeline;

float rotation_speed = 0.8f;
float rotation_angle = 0.0f;
double previous_time = -1.0;

Uniforms uniforms;

}

bool initialize() {
	const scene::Mesh mesh = scene::truncatedTetrahedron();
	index_count = uint32_t(mesh.indices.size());

	const VkDeviceSize vertices_size = mesh.vertices.size() * sizeof(scene::Vertex);
	const VkDeviceSize indices_size = mesh.indices.size() * sizeof(uint16_t);

	if (!graphics::createBuffer(vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer) ||
	    !graphics::createBuffer(indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer) ||
	    !graphics::createBuffer(sizeof(Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniform_buffer)) {
		return false;
	}
	graphics::writeBuffer(vertex_buffer, mesh.vertices.data(), vertices_size);
	graphics::writeBuffer(index_buffer, mesh.indices.data(), indices_size);

	return graphics::createUniformSet(uniform_buffer, sizeof(Uniforms), VK_SHADER_STAGE_VERTEX_BIT,
	                                  uniform_set) &&
	       graphics::createMeshPipeline(SHADER_DIR "mesh.vert.spv", SHADER_DIR "mesh.frag.spv",
	                                    uniform_set.layout, pipeline);
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	graphics::destroyPipeline(pipeline);
	graphics::destroyUniformSet(uniform_set);
	graphics::destroyBuffer(uniform_buffer);
	graphics::destroyBuffer(index_buffer);
	graphics::destroyBuffer(vertex_buffer);
}

void update(double time) {
	const float delta_time =
		previous_time < 0.0 ? 0.0f : std::min(float(time - previous_time), max_delta_time);
	previous_time = time;

	ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Lab 1", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("Truncated tetrahedron, %.1f FPS", double(ImGui::GetIO().Framerate));
		ImGui::SliderFloat("Rotation speed", &rotation_speed, 0.0f, 3.0f, "%.2f rad/s");
	}
	ImGui::End();

	rotation_angle += rotation_speed * delta_time;

	auto& context = graphics::internal::context;
	const float aspect = float(context.swapchain_extent.width) / float(context.swapchain_extent.height);

	const glm::mat4 model = glm::rotate(glm::mat4(1.0f), rotation_angle,
	                                    glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
	const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.2f, 3.5f), glm::vec3(0.0f),
	                                   glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 0.1f, 100.0f);
	projection[1][1] = -projection[1][1];

	uniforms.mvp = projection * view * model;
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;
	if (fd.command_buffer == VK_NULL_HANDLE) {
		return;
	}

	graphics::writeBuffer(uniform_buffer, &uniforms, sizeof(Uniforms));

	vkResetCommandBuffer(fd.command_buffer, 0);

	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkBeginCommandBuffer(fd.command_buffer, &begin_info);

	const VkClearValue clear_values[] = {
		{.color = {{0.08f, 0.08f, 0.1f, 1.0f}}},
		{.depthStencil = {1.0f, 0}},
	};

	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = {.extent = context.swapchain_extent},
		.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
		.pClearValues = clear_values,
	};

	vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

	const VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = float(context.swapchain_extent.width),
		.height = float(context.swapchain_extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	const VkRect2D scissor = {
		.extent = context.swapchain_extent,
	};

	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);

	const VkDeviceSize vertex_offset = 0;
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer.buffer, &vertex_offset);
	vkCmdBindIndexBuffer(fd.command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout,
	                        0, 1, &uniform_set.set, 0, nullptr);

	vkCmdDrawIndexed(fd.command_buffer, index_count, 1, 0, 0, 0);

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

}
