#include "geometry.hpp"

#include <algorithm>
#include <array>

#include <glm/geometric.hpp>

namespace scene {

namespace {

const std::array<glm::vec3, 4> tetrahedron_corners = {
	glm::vec3(1.0f, 1.0f, 1.0f),
	glm::vec3(1.0f, -1.0f, -1.0f),
	glm::vec3(-1.0f, 1.0f, -1.0f),
	glm::vec3(-1.0f, -1.0f, 1.0f),
};

uint16_t cutVertex(uint16_t corner, uint16_t towards) {
	const uint16_t neighbour = towards < corner ? towards : uint16_t(towards - 1);
	return uint16_t(corner * 3 + neighbour);
}

void addFace(Mesh& mesh, std::vector<uint16_t> face) {
	glm::vec3 center(0.0f);
	for (uint16_t index : face) {
		center += mesh.vertices[index].position;
	}
	center /= float(face.size());

	const glm::vec3& a = mesh.vertices[face[0]].position;
	const glm::vec3& b = mesh.vertices[face[1]].position;
	const glm::vec3& c = mesh.vertices[face[2]].position;
	if (glm::dot(glm::cross(b - a, c - a), center) < 0.0f) {
		std::reverse(face.begin(), face.end());
	}

	for (size_t i = 1; i + 1 < face.size(); ++i) {
		mesh.indices.push_back(face[0]);
		mesh.indices.push_back(face[i]);
		mesh.indices.push_back(face[i + 1]);
	}
}

}

Mesh truncatedTetrahedron() {
	Mesh mesh;

	for (uint16_t corner = 0; corner < 4; ++corner) {
		for (uint16_t towards = 0; towards < 4; ++towards) {
			if (towards == corner) {
				continue;
			}
			const glm::vec3 position = glm::normalize(
				2.0f * tetrahedron_corners[corner] + tetrahedron_corners[towards]);
			mesh.vertices.push_back({position, position * 0.5f + 0.5f});
		}
	}

	for (uint16_t corner = 0; corner < 4; ++corner) {
		std::vector<uint16_t> triangle;
		std::vector<uint16_t> others;
		for (uint16_t other = 0; other < 4; ++other) {
			if (other != corner) {
				triangle.push_back(cutVertex(corner, other));
				others.push_back(other);
			}
		}
		addFace(mesh, triangle);

		const uint16_t a = others[0];
		const uint16_t b = others[1];
		const uint16_t c = others[2];
		addFace(mesh, {
			cutVertex(a, b), cutVertex(b, a),
			cutVertex(b, c), cutVertex(c, b),
			cutVertex(c, a), cutVertex(a, c),
		});
	}

	return mesh;
}

}
