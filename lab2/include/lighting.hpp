#pragma once

#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

namespace scene {

struct DirectionalLight {
	float azimuth = glm::radians(40.0f);
	float elevation = glm::radians(45.0f);
	glm::vec3 color{1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
};

struct AmbientLight {
	glm::vec3 color{1.0f, 1.0f, 1.0f};
	float intensity = 0.15f;
};

struct Material {
	glm::vec3 albedo{0.85f, 0.45f, 0.30f};
	glm::vec3 specular{1.0f, 1.0f, 1.0f};
	float shininess = 32.0f;
};

glm::vec3 rayDirection(const DirectionalLight& light);

}
