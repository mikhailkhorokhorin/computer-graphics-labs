#version 450

// Вершина -> clip-пространство через MVP, цвет грани дальше во фрагментный шейдер

// Uniform-буфер из набора 0, слот 0 — тот, что привязан дескриптором в application.cpp
layout(set = 0, binding = 0) uniform Uniforms {
	mat4 mvp;
} uniforms;

// Атрибуты вершины из вершинного буфера (номера как в VkVertexInputAttributeDescription)
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

// Передаётся во фрагментный шейдер
layout(location = 0) out vec3 out_color;

void main() {
	// w = 1, потому что это точка; дальше GPU сам делит на w и переводит в пиксели
	gl_Position = uniforms.mvp * vec4(in_position, 1.0);
	out_color = in_color;
}
