#version 450

layout(set = 0, binding = 0, std140) uniform Uniforms {
	mat4 mvp;
} uniforms;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 out_color;

void main() {
	gl_Position = uniforms.mvp * vec4(in_position, 1.0);
	out_color = in_color;
}
