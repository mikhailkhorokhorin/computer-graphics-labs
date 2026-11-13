#version 450

// Выполняется для каждого пикселя треугольника: красит его в цвет грани

layout(location = 0) in vec3 in_color;

// Выход 0 — цветовая картинка render pass
layout(location = 0) out vec4 out_color;

void main() {
	// Альфа 1 — непрозрачный
	out_color = vec4(in_color, 1.0);
}
