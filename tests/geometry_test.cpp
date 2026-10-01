#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>

#include "geometry.hpp"

namespace {

constexpr float epsilon = 1e-5f;

TEST(TruncatedTetrahedron, HasTwelveVerticesAndTwentyTriangles) {
	const scene::Mesh mesh = scene::truncatedTetrahedron();
	EXPECT_EQ(mesh.vertices.size(), 12u);
	EXPECT_EQ(mesh.indices.size(), 60u);
}

TEST(TruncatedTetrahedron, VerticesLieOnUnitSphere) {
	const scene::Mesh mesh = scene::truncatedTetrahedron();
	for (const scene::Vertex& vertex : mesh.vertices) {
		EXPECT_NEAR(glm::length(vertex.position), 1.0f, epsilon);
	}
}

TEST(TruncatedTetrahedron, HasEighteenEqualEdgesWithThreePerVertex) {
	const scene::Mesh mesh = scene::truncatedTetrahedron();

	float shortest = INFINITY;
	for (size_t i = 0; i < mesh.vertices.size(); ++i) {
		for (size_t j = i + 1; j < mesh.vertices.size(); ++j) {
			shortest = std::min(shortest,
				glm::distance(mesh.vertices[i].position, mesh.vertices[j].position));
		}
	}

	std::vector<int> degree(mesh.vertices.size(), 0);
	int edges = 0;
	for (size_t i = 0; i < mesh.vertices.size(); ++i) {
		for (size_t j = i + 1; j < mesh.vertices.size(); ++j) {
			if (std::abs(glm::distance(mesh.vertices[i].position, mesh.vertices[j].position) -
			             shortest) < epsilon) {
				++edges;
				++degree[i];
				++degree[j];
			}
		}
	}

	EXPECT_EQ(edges, 18);
	for (int d : degree) {
		EXPECT_EQ(d, 3);
	}
}

TEST(TruncatedTetrahedron, TrianglesFaceOutwards) {
	const scene::Mesh mesh = scene::truncatedTetrahedron();
	for (size_t i = 0; i < mesh.indices.size(); i += 3) {
		const glm::vec3& a = mesh.vertices[mesh.indices[i]].position;
		const glm::vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
		const glm::vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
		const glm::vec3 normal = glm::cross(b - a, c - a);
		EXPECT_GT(glm::dot(normal, (a + b + c) / 3.0f), 0.0f) << "triangle " << i / 3;
	}
}

TEST(TruncatedTetrahedron, SurfaceIsClosedWithConsistentWinding) {
	const scene::Mesh mesh = scene::truncatedTetrahedron();
	std::map<std::pair<uint16_t, uint16_t>, int> directed_edges;
	for (size_t i = 0; i < mesh.indices.size(); i += 3) {
		for (size_t k = 0; k < 3; ++k) {
			++directed_edges[{mesh.indices[i + k], mesh.indices[i + (k + 1) % 3]}];
		}
	}
	for (const auto& [edge, count] : directed_edges) {
		EXPECT_EQ(count, 1);
		const auto reverse = directed_edges.find({edge.second, edge.first});
		ASSERT_NE(reverse, directed_edges.end());
		EXPECT_EQ(reverse->second, 1);
	}
}

TEST(TruncatedTetrahedron, ColorsAreDerivedFromPositions) {
	const scene::Mesh mesh = scene::truncatedTetrahedron();
	for (const scene::Vertex& vertex : mesh.vertices) {
		for (int axis = 0; axis < 3; ++axis) {
			EXPECT_GE(vertex.color[axis], 0.0f);
			EXPECT_LE(vertex.color[axis], 1.0f);
			EXPECT_NEAR(vertex.color[axis], vertex.position[axis] * 0.5f + 0.5f, epsilon);
		}
	}
}

}
