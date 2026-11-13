#include "application.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

#include <imgui.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>

namespace application {

namespace {

// ===== Данные: формат вершины, uniform-буфер, объекты Vulkan, состояние вращения =====

// Одна вершина: позиция и цвет грани (в шейдере это location 0 и 1)
struct Vertex {
	glm::vec3 position;
	glm::vec3 color;
};

// То, что лежит в uniform-буфере: одна матрица MVP, её читает вершинный шейдер
struct Uniforms {
	glm::mat4 mvp;
};

// Буфер Vulkan + его память (выделяет VMA) + указатель на эту память для записи с CPU
struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* mapped = nullptr;
};

Buffer vertex_buffer, index_buffer, uniform_buffer;
uint32_t index_count = 0;

// Дескрипторы говорят шейдеру, где лежит uniform-буфер; конвейер — все настройки рисования
VkDescriptorSetLayout set_layout;
VkDescriptorPool descriptor_pool;
VkDescriptorSet descriptor_set;
VkPipelineLayout pipeline_layout;
VkPipeline pipeline;

// Скорость меняется ползунком в меню, угол накапливается каждый кадр
float rotation_speed = 0.8f;
float rotation_angle = 0.0f;
double previous_time = -1.0;
Uniforms uniforms;

// ===== Фигура: усечённый тетраэдр (4 треугольника + 4 шестиугольника) =====

void buildMesh(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices) {
	// Правильный тетраэдр — вершины куба через одну
	const glm::vec3 corners[4] = {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}};
	const glm::vec3 colors[8] = {
		{0.95f, 0.35f, 0.30f}, {0.30f, 0.55f, 0.95f}, {0.95f, 0.80f, 0.25f}, {0.35f, 0.80f, 0.45f},
		{0.85f, 0.45f, 0.90f}, {0.30f, 0.85f, 0.85f}, {0.95f, 0.60f, 0.30f}, {0.65f, 0.65f, 0.70f},
	};

	// Точка среза на ребре i -> j на трети длины: (2*A_i + A_j) / 3.
	// normalize вместо деления на 3: все такие точки одной длины, фигура просто масштабируется
	auto cut = [&](int i, int j) { return glm::normalize(2.0f * corners[i] + corners[j]); };

	auto addFace = [&](std::vector<glm::vec3> points, glm::vec3 color) {
		// Нормаль должна смотреть наружу (туда же, куда центр грани), иначе разворачиваем обход.
		// Тогда все грани идут против часовой снаружи — это нужно для отсечения задних граней
		glm::vec3 center(0.0f);
		for (auto& p : points) center += p;
		glm::vec3 normal = glm::cross(points[1] - points[0], points[2] - points[0]);
		if (glm::dot(normal, center) < 0.0f) std::reverse(points.begin(), points.end());

		// У каждой грани свои вершины, поэтому у грани свой цвет
		const auto first = uint16_t(vertices.size());
		for (auto& p : points) vertices.push_back({p, color});
		// Многоугольник режем веером на треугольники: (0,1,2), (0,2,3), ...
		for (size_t i = 1; i + 1 < points.size(); ++i) {
			indices.insert(indices.end(), {first, uint16_t(first + i), uint16_t(first + i + 1)});
		}
	};

	// Для каждой вершины i тетраэдра: a, b, c — три остальные
	for (int i = 0; i < 4; ++i) {
		const int a = (i + 1) % 4, b = (i + 2) % 4, c = (i + 3) % 4;
		// Треугольник на месте срезанного угла i
		addFace({cut(i, a), cut(i, b), cut(i, c)}, colors[i]);
		// Шестиугольник на месте бывшей грани abc: по две точки с каждого её ребра
		addFace({cut(a, b), cut(b, a), cut(b, c), cut(c, b), cut(c, a), cut(a, c)}, colors[4 + i]);
	}
}

}

// ===== Инициализация: фигура -> буферы -> дескрипторы -> конвейер =====

bool initialize() {
	// Общие объекты каркаса: устройство, аллокатор, render pass, размер окна
	auto& context = graphics::internal::context;

	// Итог: 36 вершин и 60 индексов (20 треугольников)
	std::vector<Vertex> vertices;
	std::vector<uint16_t> indices;
	buildMesh(vertices, indices);
	index_count = uint32_t(indices.size());

	// Меню ImGui крупнее в 1.8 раза
	ImGui::GetStyle().FontScaleMain = 1.8f;
	ImGui::GetStyle().ScaleAllSizes(1.8f);

	// Буфер в памяти, которую видят и CPU, и GPU; если data передан — сразу копируем в него
	auto createBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage, const void* data, Buffer& b) {
		const VkBufferCreateInfo info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = size,
			.usage = usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};
		// HOST_ACCESS: CPU пишет в память; MAPPED: сразу получить её адрес
		const VmaAllocationCreateInfo alloc = {
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO,
		};
		VmaAllocationInfo result{};
		if (vmaCreateBuffer(context.allocator, &info, &alloc, &b.buffer, &b.allocation, &result) != VK_SUCCESS) {
			return false;
		}
		b.mapped = result.pMappedData;
		if (data) std::memcpy(b.mapped, data, size);
		return true;
	};

	// Вершины и индексы копируются один раз, uniform-буфер заполняется каждый кадр
	const VkDeviceSize vertices_size = vertices.size() * sizeof(Vertex);
	const VkDeviceSize indices_size = indices.size() * sizeof(uint16_t);
	bool ok = createBuffer(vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices.data(), vertex_buffer);
	ok = ok && createBuffer(indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data(), index_buffer);
	ok = ok && createBuffer(sizeof(Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr, uniform_buffer);
	if (!ok) return false;

	// Раскладка набора дескрипторов: в слоте 0 лежит uniform-буфер для вершинного шейдера
	const VkDescriptorSetLayoutBinding set_binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	};
	const VkDescriptorSetLayoutCreateInfo set_layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &set_binding,
	};
	if (vkCreateDescriptorSetLayout(context.device, &set_layout_info, nullptr, &set_layout) != VK_SUCCESS) {
		return false;
	}

	// Пул, из которого выделяются наборы: нам нужен 1 набор с 1 uniform-буфером
	const VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};
	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
		return false;
	}

	// Сам набор по этой раскладке, пока пустой
	const VkDescriptorSetAllocateInfo set_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &set_layout,
	};
	if (vkAllocateDescriptorSets(context.device, &set_alloc_info, &descriptor_set) != VK_SUCCESS) {
		return false;
	}

	// Кладём в слот 0 ссылку на наш uniform-буфер
	const VkDescriptorBufferInfo buffer_info = {uniform_buffer.buffer, 0, sizeof(Uniforms)};
	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descriptor_set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &buffer_info,
	};
	vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);

	// Раскладка конвейера: какие наборы дескрипторов он принимает
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

	// Две программируемые стадии: вершинный и фрагментный шейдеры
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

	// Формат вершинного буфера: шаг 24 байта, атрибут 0 — позиция, атрибут 1 — цвет
	const VkVertexInputBindingDescription vertex_binding = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
	const VkVertexInputAttributeDescription attributes[] = {
		{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
		{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
	};
	const VkPipelineVertexInputStateCreateInfo input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vertex_binding,
		.vertexAttributeDescriptionCount = 2,
		.pVertexAttributeDescriptions = attributes,
	};
	// Каждые три индекса — отдельный треугольник
	const VkPipelineInputAssemblyStateCreateInfo assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};
	// Одна область вывода; её размер задаём каждый кадр (динамическое состояние ниже)
	const VkPipelineViewportStateCreateInfo viewport = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};
	// Задние грани отбрасываем; передняя — та, что идёт против часовой на экране
	const VkPipelineRasterizationStateCreateInfo raster = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};
	// Без сглаживания: одна выборка на пиксель
	const VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};
	// Тест глубины: пиксель рисуется, только если он ближе уже нарисованного
	const VkPipelineDepthStencilStateCreateInfo depth = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};
	// Смешивания нет, пишем все каналы R, G, B, A
	const VkPipelineColorBlendAttachmentState blend_attachment = {.colorWriteMask = 0xF};
	const VkPipelineColorBlendStateCreateInfo blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blend_attachment,
	};
	// Viewport и scissor задаются командами, поэтому окно можно растягивать без пересоздания конвейера
	const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	const VkPipelineDynamicStateCreateInfo dynamic = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamic_states,
	};

	// Собираем всё в один конвейер; render pass даёт каркас (картинка цвета + буфер глубины)
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
	// Код шейдеров уже скопирован в конвейер, модули больше не нужны
	vkDestroyShaderModule(context.device, vert, nullptr);
	vkDestroyShaderModule(context.device, frag, nullptr);
	return vert && frag && result == VK_SUCCESS;
}

// ===== Завершение: ждём GPU и удаляем всё в обратном порядке =====

void shutdown() {
	auto& context = graphics::internal::context;
	// Нельзя удалять то, чем GPU ещё пользуется
	vkQueueWaitIdle(context.graphics_queue);
	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
	// Набор дескрипторов удаляется вместе с пулом
	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, set_layout, nullptr);
	vmaDestroyBuffer(context.allocator, uniform_buffer.buffer, uniform_buffer.allocation);
	vmaDestroyBuffer(context.allocator, index_buffer.buffer, index_buffer.allocation);
	vmaDestroyBuffer(context.allocator, vertex_buffer.buffer, vertex_buffer.allocation);
}

// ===== Кадр на CPU: время -> меню -> угол -> матрица MVP =====

void update(double time) {
	// Время с прошлого кадра; не больше 0.1 с, чтобы после паузы фигура не прыгала
	const float dt = previous_time < 0 ? 0.0f : std::min(float(time - previous_time), 0.1f);
	previous_time = time;

	// Меню описывается заново каждый кадр; ползунок сам меняет rotation_speed
	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
	ImGui::Begin("Lab 1", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::Text("Truncated tetrahedron, %.1f FPS", double(ImGui::GetIO().Framerate));
	ImGui::SliderFloat("Rotation speed", &rotation_speed, 0.0f, 3.0f, "%.2f rad/s");
	ImGui::End();

	// Угол копится, поэтому смена скорости не даёт рывков
	rotation_angle += rotation_speed * dt;

	const auto extent = graphics::internal::context.swapchain_extent;
	const float aspect = float(extent.width) / float(extent.height);
	const glm::vec3 axis = glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f));
	const glm::vec3 eye(0.0f, 1.2f, 3.5f);

	// M — поворот фигуры, V — камера смотрит в начало координат, P — перспектива 60°
	const glm::mat4 model = glm::rotate(glm::mat4(1.0f), rotation_angle, axis);
	const glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	// ZO — глубина в [0, 1], как в Vulkan
	glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 0.1f, 100.0f);
	// В Vulkan ось Y экрана смотрит вниз, а glm рассчитана на OpenGL — переворачиваем
	projection[1][1] *= -1.0f;

	// Применяется справа налево: сначала M, потом V, потом P
	uniforms.mvp = projection * view * model;
}

// ===== Кадр на GPU: MVP в буфер -> очистка -> рисование фигуры =====

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;
	const VkCommandBuffer cmd = fd.command_buffer;
	if (cmd == VK_NULL_HANDLE) return;

	// Пишем MVP в uniform-буфер: каркас уже дождался прошлого кадра, GPU его сейчас не читает
	std::memcpy(uniform_buffer.mapped, &uniforms, sizeof(Uniforms));
	vmaFlushAllocation(context.allocator, uniform_buffer.allocation, 0, sizeof(Uniforms));

	// Записываем команды заново; ONE_TIME_SUBMIT — буфер отправится один раз
	vkResetCommandBuffer(cmd, 0);
	const VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd, &begin);

	// Начало прохода: цвет заливаем фоном, глубину — 1.0 («дальше всего»)
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

	// Рисуем на всё окно текущего размера
	const VkExtent2D extent = context.swapchain_extent;
	const VkViewport viewport = {0, 0, float(extent.width), float(extent.height), 0.0f, 1.0f};
	const VkRect2D scissor = {{0, 0}, extent};
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// Подключаем конвейер, вершины, индексы, набор дескрипторов и рисуем 60 индексов
	const VkDeviceSize offset = 0;
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer.buffer, &offset);
	vkCmdBindIndexBuffer(cmd, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
	vkCmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);

	// Отправку на GPU и показ на экране делает каркас
	vkCmdEndRenderPass(cmd);
	vkEndCommandBuffer(cmd);
}

}
