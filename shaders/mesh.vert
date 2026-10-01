#version 450

layout(set = 0, binding = 0, std140) uniform Camera {
	mat4 view;
	mat4 projection;
} camera;

layout(set = 1, binding = 0, std140) uniform Object {
	mat4 model;
	vec4 color;
	float vertex_color_weight;
	float shading_weight;
} object;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 out_color;
layout(location = 1) out vec3 out_view_position;

void main() {
	const vec4 view_position = camera.view * object.model * vec4(in_position, 1.0);
	gl_Position = camera.projection * view_position;
	out_view_position = view_position.xyz;
	out_color = object.color.rgb * mix(vec3(1.0), in_color, object.vertex_color_weight);
}
