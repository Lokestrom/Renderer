#include "Camera.hpp"

#include <cassert>
#include <cmath>

namespace renderer {

namespace {
[[nodiscard]] glm::quat normalized(glm::quat orientation) noexcept {
	assert(glm::length(orientation) > 0.0f && "Camera orientation must be a non-zero quaternion");
	return glm::normalize(orientation);
}
}

Camera::Camera() noexcept {
	setPerspective(glm::radians(70.0f), 1.0f, 0.01f, 10000.0f);
	updateViewMatrix();
}

void Camera::setTransform(glm::vec3 position, glm::quat orientation) noexcept {
	_position = position;
	_orientation = normalized(orientation);
	updateViewMatrix();
}

void Camera::setPosition(glm::vec3 position) noexcept {
	_position = position;
	updateViewMatrix();
}

void Camera::setOrientation(glm::quat orientation) noexcept {
	_orientation = normalized(orientation);
	updateViewMatrix();
}

void Camera::translate(glm::vec3 worldDelta) noexcept {
	_position += worldDelta;
	updateViewMatrix();
}

void Camera::rotate(glm::quat localDelta) noexcept {
	_orientation = normalized(_orientation * normalized(localDelta));
	updateViewMatrix();
}

void Camera::setPerspective(float verticalFieldOfViewRadians, float aspect, float nearPlane, float farPlane) noexcept {
	assert(verticalFieldOfViewRadians > 0.0f && verticalFieldOfViewRadians < glm::pi<float>());
	assert(aspect > 0.0f);
	assert(nearPlane > 0.0f && farPlane > nearPlane);

	_projectionMatrix = glm::mat4{ 1.0f };
	_projectionMatrix[0][0] = 1.0f / (aspect * std::tan(verticalFieldOfViewRadians / 2.0f));
	_projectionMatrix[1][1] = 1.0f / std::tan(verticalFieldOfViewRadians / 2.0f);
	_projectionMatrix[2][2] = farPlane / (farPlane - nearPlane);
	_projectionMatrix[2][3] = 1.0f;
	_projectionMatrix[3][2] = -(farPlane * nearPlane) / (farPlane - nearPlane);
}

void Camera::updateViewMatrix() noexcept {
	_viewMatrix = getViewMatrix(_position, _orientation);
}

glm::mat4 getViewMatrix(glm::vec3 position, glm::quat orientation) noexcept {
	orientation = normalized(orientation);
	const glm::vec3 u = glm::vec3(1.0f - 2.0f * (orientation.y * orientation.y + orientation.z * orientation.z),
		2.0f * (orientation.x * orientation.y + orientation.z * orientation.w),
		2.0f * (orientation.x * orientation.z - orientation.y * orientation.w));

	const glm::vec3 v = glm::vec3(2.0f * (orientation.x * orientation.y - orientation.z * orientation.w),
		1.0f - 2.0f * (orientation.x * orientation.x + orientation.z * orientation.z),
		2.0f * (orientation.y * orientation.z + orientation.x * orientation.w));

	const glm::vec3 w = glm::vec3(2.0f * (orientation.x * orientation.z + orientation.y * orientation.w),
		2.0f * (orientation.y * orientation.z - orientation.x * orientation.w),
		1.0f - 2.0f * (orientation.x * orientation.x + orientation.y * orientation.y));

	glm::mat4 viewMatrix{ 1.0f };
	viewMatrix[0][0] = u.x;
	viewMatrix[1][0] = u.y;
	viewMatrix[2][0] = u.z;
	viewMatrix[0][1] = v.x;
	viewMatrix[1][1] = v.y;
	viewMatrix[2][1] = v.z;
	viewMatrix[0][2] = w.x;
	viewMatrix[1][2] = w.y;
	viewMatrix[2][2] = w.z;
	viewMatrix[3][0] = -glm::dot(u, position);
	viewMatrix[3][1] = -glm::dot(v, position);
	viewMatrix[3][2] = -glm::dot(w, position);
	return viewMatrix;
}

}