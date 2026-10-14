#include "mesh.hpp"

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

const std::array<glm::vec3, 8> face_colors = {
	glm::vec3(0.95f, 0.35f, 0.30f),
	glm::vec3(0.30f, 0.55f, 0.95f),
	glm::vec3(0.95f, 0.80f, 0.25f),
	glm::vec3(0.35f, 0.80f, 0.45f),
	glm::vec3(0.85f, 0.45f, 0.90f),
	glm::vec3(0.30f, 0.85f, 0.85f),
	glm::vec3(0.95f, 0.60f, 0.30f),
	glm::vec3(0.65f, 0.65f, 0.70f),
};

glm::vec3 cutPoint(int corner, int towards) {
	return glm::normalize(2.0f * tetrahedron_corners[corner] + tetrahedron_corners[towards]);
}

void addFace(Mesh& mesh, std::vector<glm::vec3> points, const glm::vec3& color) {
	glm::vec3 center(0.0f);
	for (const glm::vec3& point : points) {
		center += point;
	}
	center /= float(points.size());

	if (glm::dot(glm::cross(points[1] - points[0], points[2] - points[0]), center) < 0.0f) {
		std::reverse(points.begin(), points.end());
	}

	const uint16_t first = uint16_t(mesh.vertices.size());
	for (const glm::vec3& point : points) {
		mesh.vertices.push_back({point, color});
	}

	for (size_t i = 1; i + 1 < points.size(); ++i) {
		mesh.indices.push_back(first);
		mesh.indices.push_back(uint16_t(first + i));
		mesh.indices.push_back(uint16_t(first + i + 1));
	}
}

}

Mesh truncatedTetrahedron() {
	Mesh mesh;

	for (int corner = 0; corner < 4; ++corner) {
		std::vector<int> others;
		for (int other = 0; other < 4; ++other) {
			if (other != corner) {
				others.push_back(other);
			}
		}
		const int a = others[0];
		const int b = others[1];
		const int c = others[2];

		addFace(mesh, {cutPoint(corner, a), cutPoint(corner, b), cutPoint(corner, c)},
		        face_colors[corner]);

		addFace(mesh, {
			cutPoint(a, b), cutPoint(b, a),
			cutPoint(b, c), cutPoint(c, b),
			cutPoint(c, a), cutPoint(a, c),
		}, face_colors[4 + corner]);
	}

	return mesh;
}

}
