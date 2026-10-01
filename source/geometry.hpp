#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace scene {

struct Vertex {
	glm::vec3 position;
	glm::vec3 color;
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint16_t> indices;
};

Mesh truncatedTetrahedron();

}
