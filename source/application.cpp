#include "application.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <numbers>

#include <imgui.h>

#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "geometry.hpp"
#include "graphics.hpp"
#include "transform.hpp"

namespace application {

namespace {

constexpr uint32_t max_objects = 8;
constexpr float max_delta_time = 0.1f;
constexpr ImGuiSliderFlags clamp = ImGuiSliderFlags_AlwaysClamp;

enum Projection : int {
	projection_perspective,
	projection_orthographic,
};

struct CameraUniforms {
	glm::mat4 view;
	glm::mat4 projection;
};

struct ObjectUniforms {
	glm::mat4 model;
	glm::vec4 color;
	float vertex_color_weight;
	float shading_weight;
	float padding[2];
};

static_assert(sizeof(CameraUniforms) == 128);
static_assert(offsetof(ObjectUniforms, color) == 64);
static_assert(offsetof(ObjectUniforms, vertex_color_weight) == 80);
static_assert(offsetof(ObjectUniforms, shading_weight) == 84);
static_assert(sizeof(ObjectUniforms) == 96);

struct Camera {
	int projection = projection_perspective;
	float fov_y = glm::radians(60.0f);
	float half_height = 2.5f;
	float z_near = 0.1f;
	float z_far = 100.0f;
	float distance = 6.0f;
	float yaw = glm::radians(30.0f);
	float pitch = glm::radians(25.0f);
};

struct Animation {
	bool enabled = false;
	bool playing = true;
	float speed = 1.0f;
	float time = 0.0f;
	glm::vec3 spin{0.5f, 1.0f, 0.0f};
	scene::Trajectory trajectory;
};

struct Object {
	scene::Transform transform;
	glm::vec3 color{1.0f};
	bool vertex_colors = true;
	bool shading = true;
	Animation animation;
};

const std::array<glm::vec3, max_objects> palette = {
	glm::vec3(1.0f, 1.0f, 1.0f),
	glm::vec3(1.0f, 0.45f, 0.35f),
	glm::vec3(0.4f, 0.8f, 1.0f),
	glm::vec3(0.55f, 1.0f, 0.5f),
	glm::vec3(1.0f, 0.85f, 0.35f),
	glm::vec3(0.8f, 0.5f, 1.0f),
	glm::vec3(1.0f, 0.55f, 0.8f),
	glm::vec3(0.5f, 1.0f, 0.9f),
};

uint32_t index_count;
graphics::Buffer vertex_buffer;
graphics::Buffer index_buffer;
graphics::Buffer camera_buffer;
std::array<graphics::Buffer, max_objects> object_buffers;

VkDescriptorSetLayout camera_set_layout;
VkDescriptorSetLayout object_set_layout;
VkDescriptorPool descriptor_pool;
VkDescriptorSet camera_set;
std::array<VkDescriptorSet, max_objects> object_sets;

VkPipelineLayout pipeline_layout;
VkPipeline pipeline;

Camera camera;
std::array<Object, max_objects> objects;
uint32_t object_count = 1;
uint32_t selected_object = 0;
double previous_time = -1.0;
float interface_width = 0.0f;

CameraUniforms camera_uniforms;
std::array<ObjectUniforms, max_objects> object_uniforms;

VkDescriptorSetLayout createUniformSetLayout(VkShaderStageFlags stages) {
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

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	if (vkCreateDescriptorSetLayout(graphics::internal::context.device, &layout_info, nullptr,
	                                &layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor set layout\n";
	}
	return layout;
}

void writeUniformDescriptor(VkDescriptorSet set, const graphics::Buffer& buffer, VkDeviceSize size) {
	const VkDescriptorBufferInfo buffer_info = {
		.buffer = buffer.buffer,
		.offset = 0,
		.range = size,
	};

	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &buffer_info,
	};

	vkUpdateDescriptorSets(graphics::internal::context.device, 1, &write, 0, nullptr);
}

bool createDescriptors() {
	auto& context = graphics::internal::context;

	camera_set_layout = createUniformSetLayout(VK_SHADER_STAGE_VERTEX_BIT);
	object_set_layout = createUniformSetLayout(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	if (camera_set_layout == VK_NULL_HANDLE || object_set_layout == VK_NULL_HANDLE) {
		return false;
	}

	const VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1 + max_objects,
	};

	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1 + max_objects,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};

	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor pool\n";
		return false;
	}

	const VkDescriptorSetAllocateInfo camera_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &camera_set_layout,
	};

	if (vkAllocateDescriptorSets(context.device, &camera_allocate_info, &camera_set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate Vulkan descriptor set for camera\n";
		return false;
	}

	std::array<VkDescriptorSetLayout, max_objects> object_layouts;
	object_layouts.fill(object_set_layout);

	const VkDescriptorSetAllocateInfo object_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = max_objects,
		.pSetLayouts = object_layouts.data(),
	};

	if (vkAllocateDescriptorSets(context.device, &object_allocate_info,
	                             object_sets.data()) != VK_SUCCESS) {
		std::cerr << "Failed to allocate Vulkan descriptor sets for objects\n";
		return false;
	}

	writeUniformDescriptor(camera_set, camera_buffer, sizeof(CameraUniforms));
	for (uint32_t i = 0; i < max_objects; ++i) {
		writeUniformDescriptor(object_sets[i], object_buffers[i], sizeof(ObjectUniforms));
	}

	return true;
}

bool createPipeline() {
	auto& context = graphics::internal::context;

	const VkDescriptorSetLayout set_layouts[] = {
		camera_set_layout,
		object_set_layout,
	};

	const VkPipelineLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = sizeof(set_layouts) / sizeof(set_layouts[0]),
		.pSetLayouts = set_layouts,
	};

	if (vkCreatePipelineLayout(context.device, &layout_info, nullptr,
	                           &pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan pipeline layout\n";
		return false;
	}

	const VkShaderModule vertex_shader = graphics::loadShaderModule("shaders/mesh.vert.spv");
	const VkShaderModule fragment_shader = graphics::loadShaderModule("shaders/mesh.frag.spv");
	if (vertex_shader == VK_NULL_HANDLE || fragment_shader == VK_NULL_HANDLE) {
		vkDestroyShaderModule(context.device, vertex_shader, nullptr);
		vkDestroyShaderModule(context.device, fragment_shader, nullptr);
		return false;
	}

	const VkPipelineShaderStageCreateInfo stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertex_shader,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragment_shader,
			.pName = "main",
		},
	};

	const VkVertexInputBindingDescription vertex_binding = {
		.binding = 0,
		.stride = sizeof(scene::Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	const VkVertexInputAttributeDescription vertex_attributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(scene::Vertex, position),
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(scene::Vertex, color),
		},
	};

	const VkPipelineVertexInputStateCreateInfo input_state_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vertex_binding,
		.vertexAttributeDescriptionCount = sizeof(vertex_attributes) / sizeof(vertex_attributes[0]),
		.pVertexAttributeDescriptions = vertex_attributes,
	};

	const VkPipelineInputAssemblyStateCreateInfo assembly_state_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};

	const VkPipelineViewportStateCreateInfo viewport_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	const VkPipelineRasterizationStateCreateInfo raster_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	const VkPipelineMultisampleStateCreateInfo sample_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	const VkPipelineDepthStencilStateCreateInfo depth_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};

	const VkPipelineColorBlendAttachmentState attachment_info = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	const VkPipelineColorBlendStateCreateInfo blend_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &attachment_info,
	};

	const VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	const VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]),
		.pDynamicStates = dynamic_states,
	};

	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = sizeof(stages) / sizeof(stages[0]),
		.pStages = stages,
		.pVertexInputState = &input_state_info,
		.pInputAssemblyState = &assembly_state_info,
		.pViewportState = &viewport_info,
		.pRasterizationState = &raster_info,
		.pMultisampleState = &sample_info,
		.pDepthStencilState = &depth_info,
		.pColorBlendState = &blend_info,
		.pDynamicState = &dynamic_state,
		.layout = pipeline_layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};

	const VkResult result = vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1,
	                                                  &pipeline_info, nullptr, &pipeline);

	vkDestroyShaderModule(context.device, vertex_shader, nullptr);
	vkDestroyShaderModule(context.device, fragment_shader, nullptr);

	if (result != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan graphics pipeline\n";
		return false;
	}
	return true;
}

void addObject() {
	Object& object = objects[object_count];
	object = Object{};
	object.transform.scale = glm::vec3(0.5f);
	object.color = palette[object_count];
	object.animation.enabled = true;
	object.animation.time = 2.0f * std::numbers::pi_v<float> * float(object_count) / float(max_objects);
	selected_object = object_count;
	++object_count;
}

void removeSelectedObject() {
	std::move(objects.begin() + selected_object + 1, objects.begin() + object_count,
	          objects.begin() + selected_object);
	--object_count;
	selected_object = std::min(selected_object, object_count - 1);
}

void drawProjectionInterface() {
	if (!ImGui::CollapsingHeader("Projection", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::RadioButton("Perspective", &camera.projection, projection_perspective);
	ImGui::SameLine();
	ImGui::RadioButton("Orthographic", &camera.projection, projection_orthographic);

	if (camera.projection == projection_perspective) {
		ImGui::SliderAngle("Field of view", &camera.fov_y, 20.0f, 120.0f, "%.0f deg", clamp);
	} else {
		ImGui::SliderFloat("Half height", &camera.half_height, 0.5f, 10.0f, "%.3f", clamp);
	}
	ImGui::DragFloatRange2("Near / far", &camera.z_near, &camera.z_far, 0.05f, 0.01f, 200.0f,
	                       "%.3f", nullptr, clamp);
	camera.z_far = std::max(camera.z_far, camera.z_near + 0.01f);
}

void drawCameraInterface() {
	if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::SliderFloat("Distance", &camera.distance, 1.5f, 30.0f, "%.3f", clamp);
	ImGui::SliderAngle("Yaw", &camera.yaw, -180.0f, 180.0f);
	ImGui::SliderAngle("Pitch", &camera.pitch, -89.0f, 89.0f, "%.0f deg", clamp);
}

void drawObjectListInterface() {
	if (!ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	for (uint32_t i = 0; i < object_count; ++i) {
		char label[32];
		std::snprintf(label, sizeof(label), "Object %u", i + 1);
		if (ImGui::Selectable(label, selected_object == i)) {
			selected_object = i;
		}
	}

	ImGui::BeginDisabled(object_count == max_objects);
	if (ImGui::Button("Add")) {
		addObject();
	}
	ImGui::EndDisabled();

	ImGui::SameLine();

	ImGui::BeginDisabled(object_count == 1);
	if (ImGui::Button("Remove")) {
		removeSelectedObject();
	}
	ImGui::EndDisabled();
}

void drawObjectInterface(Object& object) {
	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::DragFloat3("Position", &object.transform.position.x, 0.01f);
		ImGui::SliderAngle("Rotation X", &object.transform.rotation.x, -180.0f, 180.0f);
		ImGui::SliderAngle("Rotation Y", &object.transform.rotation.y, -180.0f, 180.0f);
		ImGui::SliderAngle("Rotation Z", &object.transform.rotation.z, -180.0f, 180.0f);
		ImGui::DragFloat3("Scale", &object.transform.scale.x, 0.01f, 0.05f, 10.0f, "%.3f", clamp);
		if (ImGui::Button("Reset transform")) {
			object.transform = scene::Transform{};
		}
	}

	if (ImGui::CollapsingHeader("Color", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::ColorEdit3("Color", &object.color.x);
		ImGui::Checkbox("Vertex colors", &object.vertex_colors);
		ImGui::SameLine();
		ImGui::Checkbox("Flat shading", &object.shading);
	}

	if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
		Animation& animation = object.animation;
		scene::Trajectory& trajectory = animation.trajectory;

		ImGui::Checkbox("Move along trajectory", &animation.enabled);
		ImGui::BeginDisabled(!animation.enabled);
		if (ImGui::Button(animation.playing ? "Pause" : "Play")) {
			animation.playing = !animation.playing;
		}
		ImGui::SameLine();
		if (ImGui::Button("Restart")) {
			animation.time = 0.0f;
		}
		ImGui::SliderFloat("Speed", &animation.speed, 0.0f, 5.0f);
		ImGui::SliderFloat("Radius", &trajectory.radius, 0.0f, 5.0f);
		ImGui::SliderFloat("Loop radius", &trajectory.loop_radius, 0.0f, 2.0f);
		ImGui::SliderFloat("Loop frequency", &trajectory.loop_frequency, 0.0f, 10.0f);
		ImGui::SliderFloat("Height", &trajectory.height, 0.0f, 2.0f);
		ImGui::SliderFloat3("Spin, rad/s", &animation.spin.x, -5.0f, 5.0f);
		ImGui::EndDisabled();
	}
}

void drawInterface() {
	const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize;

	ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Scene", nullptr, flags)) {
		ImGui::Text("Truncated tetrahedron, %.1f FPS", double(ImGui::GetIO().Framerate));
		drawProjectionInterface();
		drawCameraInterface();
		drawObjectListInterface();
	}
	ImGui::End();

	const float display_width = ImGui::GetIO().DisplaySize.x;
	const ImGuiCond placement = display_width != interface_width ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
	interface_width = display_width;

	char title[32];
	std::snprintf(title, sizeof(title), "Object %u###object", selected_object + 1);
	ImGui::SetNextWindowPos(ImVec2(display_width - 10.0f, 10.0f), placement, ImVec2(1.0f, 0.0f));
	if (ImGui::Begin(title, nullptr, flags)) {
		drawObjectInterface(objects[selected_object]);
	}
	ImGui::End();
}

void updateUniforms(float delta_time) {
	auto& context = graphics::internal::context;
	const float aspect = float(context.swapchain_extent.width) / float(context.swapchain_extent.height);

	camera_uniforms.view = scene::orbitView(camera.distance, camera.yaw, camera.pitch);
	if (camera.projection == projection_perspective) {
		camera_uniforms.projection = scene::perspective(camera.fov_y, aspect, camera.z_near, camera.z_far);
	} else {
		camera_uniforms.projection = scene::orthographic(camera.half_height, aspect, camera.z_near,
		                                                 camera.z_far);
	}

	for (uint32_t i = 0; i < object_count; ++i) {
		Object& object = objects[i];
		Animation& animation = object.animation;

		scene::Transform transform = object.transform;
		if (animation.enabled) {
			if (animation.playing) {
				animation.time += delta_time * animation.speed;
			}
			transform.position += scene::trajectoryPoint(animation.trajectory, animation.time);
			transform.rotation += animation.spin * animation.time;
		}

		object_uniforms[i] = {
			.model = scene::modelMatrix(transform),
			.color = glm::vec4(object.color, 1.0f),
			.vertex_color_weight = object.vertex_colors ? 1.0f : 0.0f,
			.shading_weight = object.shading ? 1.0f : 0.0f,
		};
	}
}

}

bool initialize() {
	const scene::Mesh mesh = scene::truncatedTetrahedron();
	index_count = uint32_t(mesh.indices.size());

	const VkDeviceSize vertices_size = mesh.vertices.size() * sizeof(scene::Vertex);
	const VkDeviceSize indices_size = mesh.indices.size() * sizeof(uint16_t);

	if (!graphics::createBuffer(vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer) ||
	    !graphics::createBuffer(indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer)) {
		return false;
	}
	graphics::writeBuffer(vertex_buffer, mesh.vertices.data(), vertices_size);
	graphics::writeBuffer(index_buffer, mesh.indices.data(), indices_size);

	if (!graphics::createBuffer(sizeof(CameraUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                            camera_buffer)) {
		return false;
	}
	for (graphics::Buffer& buffer : object_buffers) {
		if (!graphics::createBuffer(sizeof(ObjectUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		                            buffer)) {
			return false;
		}
	}

	return createDescriptors() && createPipeline();
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);

	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, object_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, camera_set_layout, nullptr);

	for (graphics::Buffer& buffer : object_buffers) {
		graphics::destroyBuffer(buffer);
	}
	graphics::destroyBuffer(camera_buffer);
	graphics::destroyBuffer(index_buffer);
	graphics::destroyBuffer(vertex_buffer);
}

void update(double time) {
	const float delta_time =
		previous_time < 0.0 ? 0.0f : std::min(float(time - previous_time), max_delta_time);
	previous_time = time;

	drawInterface();
	updateUniforms(delta_time);
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;
	if (fd.command_buffer == VK_NULL_HANDLE) {
		return;
	}

	graphics::writeBuffer(camera_buffer, &camera_uniforms, sizeof(CameraUniforms));
	for (uint32_t i = 0; i < object_count; ++i) {
		graphics::writeBuffer(object_buffers[i], &object_uniforms[i], sizeof(ObjectUniforms));
	}

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

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	const VkDeviceSize vertex_offset = 0;
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer.buffer, &vertex_offset);
	vkCmdBindIndexBuffer(fd.command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);

	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
	                        0, 1, &camera_set, 0, nullptr);

	for (uint32_t i = 0; i < object_count; ++i) {
		vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
		                        1, 1, &object_sets[i], 0, nullptr);
		vkCmdDrawIndexed(fd.command_buffer, index_count, 1, 0, 0, 0);
	}

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

}
