#pragma once

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "ImGui/imgui.h"

class Camera {
public:
	Camera() noexcept;

	Camera(Camera&) = delete;
	Camera& operator=(Camera&) = delete;

	Camera(Camera&&) noexcept = default;
	Camera& operator=(Camera&&) noexcept = default;

	const glm::mat4& getProjection() const noexcept { return projectionMatrix; }
	const glm::mat4& getView() const noexcept { return viewMatrix; }
	const glm::vec3 getPosition() const noexcept { return position; }
	glm::vec3& getPositionRef() noexcept { return position; }
	const glm::quat& getOrientation() const noexcept { return orientation; }

	void update();

	void toggleMovementUpdating(bool active) noexcept { cameraMovementEnabled = active; }
	bool movementEnabled() const noexcept { return cameraMovementEnabled; }

	void updateViewMatrix();

private:
	void setPerspectiveProjection(float fovy, float aspect, float near, float far);
	void createViewMatrix(const glm::vec3& w, const glm::vec3& u, const glm::vec3& v);

private:
	bool cameraMovementEnabled = false;

	glm::vec3 position;
	glm::quat orientation;

	glm::vec2 _lastMousePosition;

	float _moveSpeed = 20.0f;
	float _rotationSpeed = 0.01f;
	float _mouseSensitivity = 1.0f;

	glm::mat4 projectionMatrix{ 1.f };
	glm::mat4 viewMatrix{ 1.f };
};

glm::mat4 getViewMatrix(glm::vec3 position, glm::quat orientation) noexcept;