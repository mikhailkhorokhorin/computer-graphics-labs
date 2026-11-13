#version 450

// Блинн-Фонг: ambient + diffuse + specular от одного направленного источника

// Те же поля и в том же порядке, что в struct Uniforms в application.cpp (std140)
layout(set = 0, binding = 0, std140) uniform Uniforms {
	mat4 model;
	mat4 view_projection;
	mat4 normal_matrix;
	vec4 camera_position;
	vec4 light_direction;
	vec4 light_color;
	vec4 ambient_color;
	vec4 albedo;
	vec4 specular;
} uniforms;

layout(location = 0) in vec3 in_world_position;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec4 out_color;

void main() {
	// n — нормаль (после интерполяции длина сбивается, нормализуем), l — на свет,
	// v — на камеру, h — половинный вектор между l и v
	const vec3 n = normalize(in_normal);
	const vec3 l = normalize(-uniforms.light_direction.xyz);
	const vec3 v = normalize(uniforms.camera_position.xyz - in_world_position);
	const vec3 h = normalize(l + v);

	// Цвет света с учётом интенсивности (она лежит в w)
	const vec3 light = uniforms.light_color.rgb * uniforms.light_color.w;
	const vec3 albedo = uniforms.albedo.rgb;
	const float n_dot_l = max(dot(n, l), 0.0);

	// ambient — свет отовсюду; diffuse — по Ламберту, ярче, когда свет падает прямо;
	// specular — блик, тем уже, чем больше блеск (specular.w); на теневой стороне блика нет
	const vec3 ambient = uniforms.ambient_color.rgb * uniforms.ambient_color.w * albedo;
	const vec3 diffuse = albedo * n_dot_l * light;
	vec3 specular = vec3(0.0);
	if (n_dot_l > 0.0) {
		specular = uniforms.specular.rgb * pow(max(dot(n, h), 0.0), uniforms.specular.w) * light;
	}

	out_color = vec4(ambient + diffuse + specular, 1.0);
}
