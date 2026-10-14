#include "camera.hpp"

#include <cmath>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace scene {

glm::vec3 forward(const Camera& camera) {
	return {
		std::cos(camera.pitch) * std::sin(camera.yaw),
		std::sin(camera.pitch),
		-std::cos(camera.pitch) * std::cos(camera.yaw),
	};
}

glm::mat4 viewMatrix(const Camera& camera) {
	return glm::lookAt(camera.position, camera.position + forward(camera), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 projectionMatrix(const Camera& camera, float aspect) {
	glm::mat4 projection = glm::perspectiveRH_ZO(camera.fov_y, aspect, 0.1f, 100.0f);
	projection[1][1] = -projection[1][1];
	return projection;
}

}
