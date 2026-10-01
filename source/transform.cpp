#include "transform.hpp"

#include <cmath>

#include <glm/ext/matrix_transform.hpp>

namespace scene {

glm::mat4 modelMatrix(const Transform& transform) {
	glm::mat4 model(1.0f);
	model = glm::translate(model, transform.position);
	model = glm::rotate(model, transform.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
	model = glm::rotate(model, transform.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
	model = glm::rotate(model, transform.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
	model = glm::scale(model, transform.scale);
	return model;
}

glm::vec3 trajectoryPoint(const Trajectory& trajectory, float t) {
	const float k = trajectory.loop_frequency;
	return {
		trajectory.radius * std::cos(t) + trajectory.loop_radius * std::cos(k * t),
		trajectory.height * std::sin(2.0f * t),
		trajectory.radius * std::sin(t) + trajectory.loop_radius * std::sin(k * t),
	};
}

glm::mat4 orbitView(float distance, float yaw, float pitch) {
	const glm::vec3 eye = distance * glm::vec3(
		std::cos(pitch) * std::sin(yaw),
		std::sin(pitch),
		std::cos(pitch) * std::cos(yaw));
	return glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 perspective(float fov_y, float aspect, float z_near, float z_far) {
	const float f = 1.0f / std::tan(fov_y / 2.0f);

	glm::mat4 projection(0.0f);
	projection[0][0] = f / aspect;
	projection[1][1] = -f;
	projection[2][2] = z_far / (z_near - z_far);
	projection[2][3] = -1.0f;
	projection[3][2] = z_near * z_far / (z_near - z_far);
	return projection;
}

glm::mat4 orthographic(float half_height, float aspect, float z_near, float z_far) {
	glm::mat4 projection(1.0f);
	projection[0][0] = 1.0f / (half_height * aspect);
	projection[1][1] = -1.0f / half_height;
	projection[2][2] = 1.0f / (z_near - z_far);
	projection[3][2] = z_near / (z_near - z_far);
	return projection;
}

}
