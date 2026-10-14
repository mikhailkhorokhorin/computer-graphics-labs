#include "application.hpp"

#include <algorithm>

#include <imgui.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

#include "buffer.hpp"
#include "descriptor.hpp"
#include "pipeline.hpp"
#include "camera.hpp"
#include "lighting.hpp"
#include "mesh.hpp"

namespace application {

namespace {

constexpr float max_delta_time = 0.1f;
constexpr ImGuiSliderFlags clamp = ImGuiSliderFlags_AlwaysClamp;

struct Uniforms {
	glm::mat4 model;
	glm::mat4 view_projection;
	glm::mat4 normal_matrix;
	glm::vec4 camera_position;
	glm::vec4 light_direction;
	glm::vec4 light_color;
	glm::vec4 ambient_color;
	glm::vec4 albedo;
	glm::vec4 specular;
};

static_assert(sizeof(Uniforms) == 3 * 64 + 6 * 16);

uint32_t index_count;
graphics::Buffer vertex_buffer;
graphics::Buffer index_buffer;
graphics::Buffer uniform_buffer;
graphics::UniformSet uniform_set;
graphics::Pipeline pipeline;

scene::Camera camera;
scene::DirectionalLight light;
scene::AmbientLight ambient;
scene::Material material;

float rotation_speed = 0.3f;
float rotation_angle = 0.0f;
double previous_time = -1.0;

Uniforms uniforms;

void drawInterface() {
	ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Lab 2", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("Truncated tetrahedron, %.1f FPS", double(ImGui::GetIO().Framerate));

		if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::DragFloat3("Position", &camera.position.x, 0.01f);
			ImGui::SliderAngle("Yaw", &camera.yaw, -180.0f, 180.0f);
			ImGui::SliderAngle("Pitch", &camera.pitch, -89.0f, 89.0f, "%.0f deg", clamp);
			ImGui::SliderAngle("Field of view", &camera.fov_y, 20.0f, 120.0f, "%.0f deg", clamp);
		}

		if (ImGui::CollapsingHeader("Directional light", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderAngle("Azimuth", &light.azimuth, -180.0f, 180.0f);
			ImGui::SliderAngle("Elevation", &light.elevation, -90.0f, 90.0f, "%.0f deg", clamp);
			ImGui::ColorEdit3("Light color", &light.color.x);
			ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 5.0f, "%.2f", clamp);
		}

		if (ImGui::CollapsingHeader("Ambient light", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::ColorEdit3("Ambient color", &ambient.color.x);
			ImGui::SliderFloat("Ambient intensity", &ambient.intensity, 0.0f, 1.0f, "%.2f", clamp);
		}

		if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::ColorEdit3("Albedo", &material.albedo.x);
			ImGui::ColorEdit3("Specular color", &material.specular.x);
			ImGui::SliderFloat("Shininess", &material.shininess, 1.0f, 256.0f, "%.0f",
			                   clamp | ImGuiSliderFlags_Logarithmic);
		}

		ImGui::SliderFloat("Rotation speed", &rotation_speed, 0.0f, 3.0f, "%.2f rad/s", clamp);
	}
	ImGui::End();
}

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

	return graphics::createUniformSet(uniform_buffer, sizeof(Uniforms),
	                                  VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
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

	drawInterface();

	rotation_angle += rotation_speed * delta_time;

	auto& context = graphics::internal::context;
	const float aspect = float(context.swapchain_extent.width) / float(context.swapchain_extent.height);

	const glm::mat4 model = glm::rotate(glm::mat4(1.0f), rotation_angle,
	                                    glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));

	uniforms = {
		.model = model,
		.view_projection = scene::projectionMatrix(camera, aspect) * scene::viewMatrix(camera),
		.normal_matrix = glm::mat4(glm::transpose(glm::inverse(glm::mat3(model)))),
		.camera_position = glm::vec4(camera.position, 1.0f),
		.light_direction = glm::vec4(scene::rayDirection(light), 0.0f),
		.light_color = glm::vec4(light.color, light.intensity),
		.ambient_color = glm::vec4(ambient.color, ambient.intensity),
		.albedo = glm::vec4(material.albedo, 1.0f),
		.specular = glm::vec4(material.specular, material.shininess),
	};
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
