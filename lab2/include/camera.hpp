#pragma once

#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

namespace scene {

struct Camera {
	glm::vec3 position{0.0f, 1.0f, 3.5f};
	float yaw = 0.0f;
	float pitch = glm::radians(-15.0f);
	float fov_y = glm::radians(60.0f);
};

glm::vec3 forward(const Camera& camera);
glm::mat4 viewMatrix(const Camera& camera);
glm::mat4 projectionMatrix(const Camera& camera, float aspect);

}
