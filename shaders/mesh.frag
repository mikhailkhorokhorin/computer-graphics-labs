#version 450

layout(set = 1, binding = 0, std140) uniform Object {
	mat4 model;
	vec4 color;
	float vertex_color_weight;
	float shading_weight;
} object;

layout(location = 0) in vec3 in_color;
layout(location = 1) in vec3 in_view_position;

layout(location = 0) out vec4 out_color;

const vec3 light_direction = normalize(vec3(-0.4, 0.6, 0.7));

void main() {
	vec3 normal = normalize(cross(dFdx(in_view_position), dFdy(in_view_position)));
	if (dot(normal, in_view_position) > 0.0) {
		normal = -normal;
	}
	const float diffuse = max(dot(normal, light_direction), 0.0);
	const float lighting = mix(1.0, 0.35 + 0.65 * diffuse, object.shading_weight);
	out_color = vec4(in_color * lighting, 1.0);
}
