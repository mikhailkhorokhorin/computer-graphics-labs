#include "lighting.hpp"

#include <cmath>

namespace scene {

glm::vec3 rayDirection(const DirectionalLight& light) {
	const glm::vec3 to_light(std::cos(light.elevation) * std::sin(light.azimuth),
	                         std::sin(light.elevation),
	                         std::cos(light.elevation) * std::cos(light.azimuth));
	return -to_light;
}

}
