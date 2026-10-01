#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace scene {

struct Transform {
	glm::vec3 position{0.0f};
	glm::vec3 rotation{0.0f};
	glm::vec3 scale{1.0f};
};

struct Trajectory {
	float radius = 2.0f;
	float loop_radius = 0.6f;
	float loop_frequency = 5.0f;
	float height = 0.5f;
};

glm::mat4 modelMatrix(const Transform& transform);
glm::vec3 trajectoryPoint(const Trajectory& trajectory, float t);

glm::mat4 orbitView(float distance, float yaw, float pitch);
glm::mat4 perspective(float fov_y, float aspect, float z_near, float z_far);
glm::mat4 orthographic(float half_height, float aspect, float z_near, float z_far);

}
