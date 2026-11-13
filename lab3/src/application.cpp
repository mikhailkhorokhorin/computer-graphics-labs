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

// ===== Данные: формат вершины, uniform-буфер, объекты Vulkan, параметры сцены из меню =====

// К позиции и нормали добавились текстурные координаты uv (точка на картинке, от 0 до 1)
struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 uv;
};

// Как в лабе 2, но без albedo — основной цвет теперь берётся из текстуры
struct Uniforms {
	glm::mat4 model;
	glm::mat4 view_projection;
	glm::mat4 normal_matrix;
	glm::vec4 camera_position;
	glm::vec4 light_direction;
	glm::vec4 light_color;
	glm::vec4 ambient_color;
	glm::vec4 specular;
};

// Буфер Vulkan + его память (VMA) + указатель для CPU (только у буферов в RAM)
struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* mapped = nullptr;
};

Buffer vertex_buffer, index_buffer, uniform_buffer;
uint32_t index_count = 0;

// Текстура: изображение в памяти GPU, «вид» на него для шейдера и сэмплер (как читать пиксели)
VkImage texture_image;
VmaAllocation texture_allocation;
VkImageView texture_view;
VkSampler sampler;

VkDescriptorSetLayout set_layout;
VkDescriptorPool descriptor_pool;
VkDescriptorSet descriptor_set;
VkPipelineLayout pipeline_layout;
VkPipeline pipeline;

// Камера: позиция, поворот и угол обзора
glm::vec3 camera_position(0.0f, 1.0f, 3.5f);
float camera_yaw = 0.0f;
float camera_pitch = glm::radians(-15.0f);
float camera_fov = glm::radians(60.0f);

// Направленный свет: откуда светит, цвет и интенсивность
float light_azimuth = glm::radians(40.0f);
float light_elevation = glm::radians(45.0f);
glm::vec3 light_color(1.0f);
float light_intensity = 1.0f;

// Рассеянный свет
glm::vec3 ambient_color(1.0f);
float ambient_intensity = 0.15f;

// Материал: цвет блика и блеск (альбедо — из текстуры)
glm::vec3 specular_color(0.5f);
float shininess = 32.0f;

float rotation_speed = 0.3f;
float rotation_angle = 0.0f;
double previous_time = -1.0;
Uniforms uniforms;

// ===== Фигура: усечённый тетраэдр, у каждой грани своя нормаль и плоская развёртка UV =====

void buildMesh(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices) {
	const glm::vec3 corners[4] = {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}};

	// Точка среза на ребре i -> j на трети длины, нормализованная
	auto cut = [&](int i, int j) { return glm::normalize(2.0f * corners[i] + corners[j]); };

	auto addFace = [&](std::vector<glm::vec3> points) {
		// Центр грани нужен и для проверки нормали, и для UV
		glm::vec3 center(0.0f);
		for (auto& p : points) center += p;
		center /= float(points.size());
		glm::vec3 normal = glm::normalize(glm::cross(points[1] - points[0], points[2] - points[0]));
		if (glm::dot(normal, center) < 0.0f) {
			std::reverse(points.begin(), points.end());
			normal = -normal;
		}

		// UV: две оси в плоскости грани (вдоль первого ребра и перпендикулярно ему),
		// координаты точки относительно центра; 0.55 вписывает самую большую грань в [0, 1]
		const glm::vec3 u_axis = glm::normalize(points[1] - points[0]);
		const glm::vec3 v_axis = glm::cross(normal, u_axis);
		const auto first = uint16_t(vertices.size());
		for (auto& p : points) {
			const glm::vec3 d = p - center;
			vertices.push_back({p, normal, 0.5f + 0.55f * glm::vec2(glm::dot(d, u_axis), glm::dot(d, v_axis))});
		}
		// Веер треугольников: (0,1,2), (0,2,3), ...
		for (size_t i = 1; i + 1 < points.size(); ++i) {
			indices.insert(indices.end(), {first, uint16_t(first + i), uint16_t(first + i + 1)});
		}
	};

	// Треугольник на месте каждого среза и шестиугольник на месте каждой бывшей грани
	for (int i = 0; i < 4; ++i) {
		const int a = (i + 1) % 4, b = (i + 2) % 4, c = (i + 3) % 4;
		addFace({cut(i, a), cut(i, b), cut(i, c)});
		addFace({cut(a, b), cut(b, a), cut(b, c), cut(c, b), cut(c, a), cut(a, c)});
	}
}

// ===== Ресурсы: uniform-буфер в RAM; текстура, вершины и индексы через staging-буфер в VRAM =====

bool createResources(const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices) {
	auto& context = graphics::internal::context;

	// host = true: память видна CPU (можно писать memcpy); false: память на GPU (VRAM)
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

	// lodepng читает PNG с диска и раскодирует в массив пикселей RGBA, по 4 байта на пиксель
	std::vector<unsigned char> pixels;
	unsigned width = 0, height = 0;
	if (lodepng::decode(pixels, width, height, TEXTURE_DIR "wood.png") != 0) return false;

	const VkDeviceSize pixels_size = pixels.size();
	const VkDeviceSize vertices_size = vertices.size() * sizeof(Vertex);
	const VkDeviceSize indices_size = indices.size() * sizeof(uint16_t);
	const VkBufferUsageFlags dst = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	// uniform и staging — в RAM; вершины и индексы — в VRAM, в них можно только копировать (TRANSFER_DST)
	Buffer staging;
	bool ok = createBuffer(sizeof(Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, uniform_buffer);
	ok = ok && createBuffer(vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | dst, false, vertex_buffer);
	ok = ok && createBuffer(indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | dst, false, index_buffer);
	ok = ok && createBuffer(pixels_size + vertices_size + indices_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, staging);
	if (!ok) return false;

	// В staging подряд: пиксели, потом вершины, потом индексы
	auto* staging_data = static_cast<char*>(staging.mapped);
	std::memcpy(staging_data, pixels.data(), pixels_size);
	std::memcpy(staging_data + pixels_size, vertices.data(), vertices_size);
	std::memcpy(staging_data + pixels_size + vertices_size, indices.data(), indices_size);
	vmaFlushAllocation(context.allocator, staging.allocation, 0, VK_WHOLE_SIZE);

	// Изображение в VRAM: формат SRGB (GPU сам переведёт цвета в линейные при чтении),
	// OPTIMAL — раскладка, удобная GPU; в него копируем (TRANSFER_DST) и из него читаем (SAMPLED)
	const VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_SRGB,
		.extent = {width, height, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	const VmaAllocationCreateInfo image_alloc = {.usage = VMA_MEMORY_USAGE_AUTO};
	const VkResult image_result =
		vmaCreateImage(context.allocator, &image_info, &image_alloc, &texture_image, &texture_allocation, nullptr);
	if (image_result != VK_SUCCESS) return false;

	// Свой пул и командный буфер для копирования — он отправится один раз
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

	// Барьер 1: изображение из неопределённого состояния -> цель копирования
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

	// Копирования из staging: пиксели в изображение, вершины и индексы в свои буферы
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

	// Барьер 2: копирование закончено -> изображение готово к чтению во фрагментном шейдере,
	// а вершины и индексы — к чтению на входе конвейера
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

	// Отправляем на GPU и ждём конца; потом пул и staging больше не нужны
	const VkSubmitInfo submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd,
	};
	vkQueueSubmit(context.graphics_queue, 1, &submit, VK_NULL_HANDLE);
	vkQueueWaitIdle(context.graphics_queue);
	vkDestroyCommandPool(context.device, command_pool, nullptr);
	vmaDestroyBuffer(context.allocator, staging.buffer, staging.allocation);

	// Вид на изображение (шейдер работает через него) и сэмплер:
	// LINEAR — смешивать соседние пиксели, REPEAT — повторять картинку за пределами [0, 1]
	const VkImageViewCreateInfo view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = texture_image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_SRGB,
		.subresourceRange = range,
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
	if (vkCreateImageView(context.device, &view_info, nullptr, &texture_view) != VK_SUCCESS) return false;
	return vkCreateSampler(context.device, &sampler_info, nullptr, &sampler) == VK_SUCCESS;
}

}

// ===== Инициализация: фигура -> ресурсы -> дескрипторы -> конвейер =====

bool initialize() {
	auto& context = graphics::internal::context;

	std::vector<Vertex> vertices;
	std::vector<uint16_t> indices;
	buildMesh(vertices, indices);
	index_count = uint32_t(indices.size());

	ImGui::GetStyle().FontScaleMain = 1.8f;
	ImGui::GetStyle().ScaleAllSizes(1.8f);

	if (!createResources(vertices, indices)) return false;

	// Один набор на всё: слот 0 — динамический uniform-буфер (сдвиг задаётся при привязке),
	// слот 1 — текстура вместе с сэмплером, её читает фрагментный шейдер
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
	};
	const VkDescriptorSetLayoutCreateInfo set_layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 2,
		.pBindings = set_bindings,
	};
	if (vkCreateDescriptorSetLayout(context.device, &set_layout_info, nullptr, &set_layout) != VK_SUCCESS) {
		return false;
	}

	// Пул умеет выделять оба типа дескрипторов
	const VkDescriptorPoolSize pool_sizes[] = {
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
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

	// Слот 0 — uniform-буфер (одна структура), слот 1 — сэмплер + вид на текстуру
	const VkDescriptorBufferInfo buffer_info = {uniform_buffer.buffer, 0, sizeof(Uniforms)};
	const VkDescriptorImageInfo image_info = {sampler, texture_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
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
	};
	vkUpdateDescriptorSets(context.device, 2, writes, 0, nullptr);

	const VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &set_layout,
	};
	if (vkCreatePipelineLayout(context.device, &pipeline_layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
		return false;
	}

	// Читаем SPIR-V (glslc компилирует его из GLSL при сборке) и создаём модуль шейдера
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

	// Три атрибута: позиция, нормаль, uv (два float)
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
	// Остальные настройки конвейера — как в лабах 1 и 2
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
	const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	const VkPipelineDynamicStateCreateInfo dynamic = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamic_states,
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
	VkResult result = vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
	vkDestroyShaderModule(context.device, vert, nullptr);
	vkDestroyShaderModule(context.device, frag, nullptr);
	return vert && frag && result == VK_SUCCESS;
}

// ===== Завершение: ждём GPU и удаляем всё в обратном порядке =====

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);
	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, set_layout, nullptr);
	vkDestroySampler(context.device, sampler, nullptr);
	vkDestroyImageView(context.device, texture_view, nullptr);
	vmaDestroyImage(context.allocator, texture_image, texture_allocation);
	vmaDestroyBuffer(context.allocator, uniform_buffer.buffer, uniform_buffer.allocation);
	vmaDestroyBuffer(context.allocator, index_buffer.buffer, index_buffer.allocation);
	vmaDestroyBuffer(context.allocator, vertex_buffer.buffer, vertex_buffer.allocation);
}

// ===== Кадр на CPU: время -> меню -> угол -> матрицы и параметры света =====

void update(double time) {
	const float dt = previous_time < 0 ? 0.0f : std::min(float(time - previous_time), 0.1f);
	previous_time = time;

	// Меню как в лабе 2, но без Albedo — его заменила текстура
	const ImGuiSliderFlags clamp = ImGuiSliderFlags_AlwaysClamp;
	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
	ImGui::Begin("Lab 3", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::Text("Truncated tetrahedron, %.1f FPS", double(ImGui::GetIO().Framerate));
	if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::DragFloat3("Position", &camera_position.x, 0.01f);
		ImGui::SliderAngle("Yaw", &camera_yaw, -180.0f, 180.0f);
		ImGui::SliderAngle("Pitch", &camera_pitch, -89.0f, 89.0f, "%.0f deg", clamp);
		ImGui::SliderAngle("Field of view", &camera_fov, 20.0f, 120.0f, "%.0f deg", clamp);
	}
	if (ImGui::CollapsingHeader("Directional light", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderAngle("Azimuth", &light_azimuth, -180.0f, 180.0f);
		ImGui::SliderAngle("Elevation", &light_elevation, -90.0f, 90.0f, "%.0f deg", clamp);
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
	}
	ImGui::SliderFloat("Rotation speed", &rotation_speed, 0.0f, 3.0f, "%.2f rad/s", clamp);
	ImGui::End();

	rotation_angle += rotation_speed * dt;

	const auto extent = graphics::internal::context.swapchain_extent;
	const float aspect = float(extent.width) / float(extent.height);
	const glm::vec3 axis = glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f));
	// Куда смотрит камера, из yaw и pitch
	const glm::vec3 forward = {
		std::cos(camera_pitch) * std::sin(camera_yaw),
		std::sin(camera_pitch),
		-std::cos(camera_pitch) * std::cos(camera_yaw),
	};
	// Направление на источник света
	const glm::vec3 to_light = {
		std::cos(light_elevation) * std::sin(light_azimuth),
		std::sin(light_elevation),
		std::cos(light_elevation) * std::cos(light_azimuth),
	};

	const glm::mat4 model = glm::rotate(glm::mat4(1.0f), rotation_angle, axis);
	const glm::mat4 view = glm::lookAt(camera_position, camera_position + forward, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 projection = glm::perspectiveRH_ZO(camera_fov, aspect, 0.1f, 100.0f);
	projection[1][1] *= -1.0f;

	uniforms = {
		.model = model,
		.view_projection = projection * view,
		.normal_matrix = glm::mat4(glm::transpose(glm::inverse(glm::mat3(model)))),
		.camera_position = glm::vec4(camera_position, 1.0f),
		.light_direction = glm::vec4(-to_light, 0.0f),
		.light_color = glm::vec4(light_color, light_intensity),
		.ambient_color = glm::vec4(ambient_color, ambient_intensity),
		.specular = glm::vec4(specular_color, shininess),
	};
}

// ===== Кадр на GPU: uniform-данные в буфер -> очистка -> рисование со сдвигом в динамическом буфере =====

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;
	const VkCommandBuffer cmd = fd.command_buffer;
	if (cmd == VK_NULL_HANDLE) return;

	std::memcpy(uniform_buffer.mapped, &uniforms, sizeof(Uniforms));
	vmaFlushAllocation(context.allocator, uniform_buffer.allocation, 0, sizeof(Uniforms));

	vkResetCommandBuffer(cmd, 0);
	const VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd, &begin);

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

	// dynamic_offset — откуда в uniform-буфере читать структуру этого объекта.
	// Объект один, поэтому 0; для i-го объекта было бы i * (размер структуры с выравниванием)
	const VkDeviceSize offset = 0;
	const uint32_t dynamic_offset = 0;
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer.buffer, &offset);
	vkCmdBindIndexBuffer(cmd, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set, 1, &dynamic_offset);
	vkCmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);

	vkCmdEndRenderPass(cmd);
	vkEndCommandBuffer(cmd);
}

}
