#version 450

layout(set = 0, binding = 0, std140) uniform Uniforms {
	mat4 model;
	mat4 view_projection;
	mat4 light_view_projection;
	mat4 normal_matrix;
	vec4 camera_position;
	vec4 light_direction;
	vec4 light_color;
	vec4 ambient_color;
	vec4 specular;
	vec4 albedo;
} uniforms;

layout(location = 0) in vec3 in_position;

void main() {
	gl_Position = uniforms.light_view_projection * uniforms.model * vec4(in_position, 1.0);
}
