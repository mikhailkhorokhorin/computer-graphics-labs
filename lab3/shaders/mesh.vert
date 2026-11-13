#version 450

// Вершина в мир и на экран; нормаль в мир и UV дальше во фрагментный шейдер

// Те же поля и в том же порядке, что в struct Uniforms в application.cpp (std140)
layout(set = 0, binding = 0, std140) uniform Uniforms {
	mat4 model;
	mat4 view_projection;
	mat4 normal_matrix;
	vec4 camera_position;
	vec4 light_direction;
	vec4 light_color;
	vec4 ambient_color;
	vec4 specular;
} uniforms;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec3 out_world_position;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;

void main() {
	// Точка в мировых координатах — она нужна для освещения
	const vec4 world_position = uniforms.model * vec4(in_position, 1.0);
	gl_Position = uniforms.view_projection * world_position;
	out_world_position = world_position.xyz;
	// Нормаль поворачиваем матрицей нормалей (M^-1)^T, сдвиг для направления не нужен
	out_normal = mat3(uniforms.normal_matrix) * in_normal;
	out_uv = in_uv;
}
