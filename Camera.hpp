#pragma once

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "ImGui/imgui.h"
#include "../settings.hpp"

namespace vulkan {

void createCameraSettings();

class Camera {
public:
	enum class State {
		Still,
		FreeCAM,
		Orbit
	};

	Camera() noexcept;

	Camera(Camera&) = delete;
	Camera& operator=(Camera&) = delete;

	Camera(Camera&&) noexcept = default;
	Camera& operator=(Camera&&) noexcept = default;

	const glm::mat4& getProjection() const noexcept { return _projectionMatrix; }
	const glm::mat4& getView() const noexcept { return _viewMatrix; }
	const glm::vec3& getPosition() const noexcept { return _position; }
	glm::vec3& getPositionRef() noexcept { return _position; }
	const glm::quat& getOrientation() const noexcept { return _orientation; }

	void update();

	void setState(State state) noexcept;
	State getState() const noexcept { return _state; }

	void updateViewMatrix();

private:
	void freeCAMMovement();
	void lookAtMovement();

	void setPerspectiveProjection(float fovy, float aspect, float near, float far);
	void createViewMatrix(const glm::vec3& w, const glm::vec3& u, const glm::vec3& v);

private:
	State _state = State::Orbit;

	glm::vec3 _position;
	glm::quat _orientation;

	double _moveSpeed = 20.0f;
	settings::ValueHandle<double> _mouseSensitivity;

	glm::mat4 _projectionMatrix{ 1.f };
	glm::mat4 _viewMatrix{ 1.f };

	double _radius = 1;
	double _yaw = 0;
	double _pitch = 0;

	// settings::ValueHandle<double> _minRadius;
	settings::ValueHandle<double> _zoomSpeed;
};

glm::mat4 getViewMatrix(glm::vec3 position, glm::quat orientation) noexcept;

}