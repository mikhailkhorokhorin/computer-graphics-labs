#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec4.hpp>

#include "transform.hpp"

namespace {

constexpr float epsilon = 1e-5f;
constexpr float pi = std::numbers::pi_v<float>;

void expectMatrixNear(const glm::mat4& actual, const glm::mat4& expected) {
	for (int column = 0; column < 4; ++column) {
		for (int row = 0; row < 4; ++row) {
			EXPECT_NEAR(actual[column][row], expected[column][row], epsilon)
				<< "column " << column << ", row " << row;
		}
	}
}

void expectVectorNear(const glm::vec3& actual, const glm::vec3& expected) {
	EXPECT_NEAR(actual.x, expected.x, epsilon);
	EXPECT_NEAR(actual.y, expected.y, epsilon);
	EXPECT_NEAR(actual.z, expected.z, epsilon);
}

TEST(Perspective, MatchesGlmWithFlippedY) {
	glm::mat4 expected = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
	expected[1][1] = -expected[1][1];
	expectMatrixNear(scene::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f), expected);
}

TEST(Perspective, MapsNearAndFarPlanesToZeroAndOne) {
	const glm::mat4 projection = scene::perspective(glm::radians(90.0f), 1.0f, 0.5f, 20.0f);
	const glm::vec4 near_point = projection * glm::vec4(0.0f, 0.0f, -0.5f, 1.0f);
	const glm::vec4 far_point = projection * glm::vec4(0.0f, 0.0f, -20.0f, 1.0f);
	EXPECT_NEAR(near_point.z / near_point.w, 0.0f, epsilon);
	EXPECT_NEAR(far_point.z / far_point.w, 1.0f, epsilon);
}

TEST(Perspective, PointAboveCameraGoesToTopOfScreen) {
	const glm::mat4 projection = scene::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
	const glm::vec4 point = projection * glm::vec4(0.0f, 1.0f, -1.0f, 1.0f);
	EXPECT_NEAR(point.y / point.w, -1.0f, epsilon);
}

TEST(Orthographic, MatchesGlmWithFlippedY) {
	const float aspect = 4.0f / 3.0f;
	glm::mat4 expected = glm::orthoRH_ZO(-2.0f * aspect, 2.0f * aspect, -2.0f, 2.0f, 0.1f, 50.0f);
	expected[1][1] = -expected[1][1];
	expectMatrixNear(scene::orthographic(2.0f, aspect, 0.1f, 50.0f), expected);
}

TEST(Orthographic, KeepsSizeIndependentOfDistance) {
	const glm::mat4 projection = scene::orthographic(2.0f, 1.0f, 0.1f, 50.0f);
	const glm::vec4 close = projection * glm::vec4(1.0f, 0.0f, -1.0f, 1.0f);
	const glm::vec4 distant = projection * glm::vec4(1.0f, 0.0f, -40.0f, 1.0f);
	EXPECT_NEAR(close.x / close.w, distant.x / distant.w, epsilon);
}

TEST(ModelMatrix, ScalesThenRotatesThenTranslates) {
	scene::Transform transform;
	transform.position = glm::vec3(1.0f, 0.0f, 0.0f);
	transform.rotation = glm::vec3(0.0f, pi / 2.0f, 0.0f);
	transform.scale = glm::vec3(2.0f);
	const glm::vec4 point = scene::modelMatrix(transform) * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
	expectVectorNear(glm::vec3(point), glm::vec3(1.0f, 0.0f, -2.0f));
}

TEST(ModelMatrix, IdentityTransformGivesIdentityMatrix) {
	expectMatrixNear(scene::modelMatrix(scene::Transform{}), glm::mat4(1.0f));
}

TEST(Trajectory, StartsOnPositiveXAxis) {
	const scene::Trajectory trajectory;
	expectVectorNear(scene::trajectoryPoint(trajectory, 0.0f),
	                 glm::vec3(trajectory.radius + trajectory.loop_radius, 0.0f, 0.0f));
}

TEST(Trajectory, IsPeriodicForIntegerLoopFrequency) {
	const scene::Trajectory trajectory;
	expectVectorNear(scene::trajectoryPoint(trajectory, 2.0f * pi + 0.3f),
	                 scene::trajectoryPoint(trajectory, 0.3f));
}

TEST(OrbitView, MovesEyeToOriginAndLooksDownNegativeZ) {
	const float distance = 5.0f;
	const float yaw = 0.7f;
	const float pitch = 0.3f;
	const glm::mat4 view = scene::orbitView(distance, yaw, pitch);
	const glm::vec3 eye = distance * glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
	                                           std::cos(pitch) * std::cos(yaw));
	expectVectorNear(glm::vec3(view * glm::vec4(eye, 1.0f)), glm::vec3(0.0f));
	expectVectorNear(glm::vec3(view * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)),
	                 glm::vec3(0.0f, 0.0f, -distance));
}

}
