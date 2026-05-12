#include "Camera.hpp"

#include "../App.hpp"
#include "../Settings.hpp"
#include "../Input/InputEventHandler.hpp"
#include "../gui/settingsGui.hpp"

using namespace vulkan;

void vulkan::createCameraSettings() {
	auto& cameraSettings = settings::Settings::newCategory("Camera");

	cameraSettings.emplace<settings::ValueWithRange<double>>("Mouse sensitivity", 3.0,
		settings::ValueWithRange<double>::setFunctionT(gui::slider), 0.1, 10.0);
	cameraSettings.emplace<settings::ValueWithRange<double>>("Zoom speed", 20.0,
		settings::ValueWithRange<double>::setFunctionT(gui::slider), 1.0, 50.0);
	//cameraSettings.emplace<settings::Value<bool>>("Relative rotation", false, gui::checkbox);
	//cameraSettings.emplace<settings::Value<bool>>("Flip X input", false, gui::checkbox);
	//cameraSettings.emplace<settings::Value<bool>>("Flip Y input", false, gui::checkbox);
}

Camera::Camera() noexcept
	: _projectionMatrix(1.0f)
	, _viewMatrix(1.0f)
	, _position(0.0f, 0.0f, 0.0f)
	, _orientation(1.0f, 0.0f, 0.0f, 0.0f)
	, _mouseSensitivity(settings::Settings::get("Camera").get<double>("Mouse sensitivity").getHandle())
	, _zoomSpeed(settings::Settings::get("Camera").get<double>("Zoom speed").getHandle()) {
	setPerspectiveProjection(glm::radians(70.0f), 4.0f / 3.0f, 0.1f, 100.0f);
	updateViewMatrix();
}

void Camera::update() {
	switch (_state) {
	case State::Still:
		break;
	case State::FreeCAM:
		freeCAMMovement();
		break;
	case State::Orbit:
		lookAtMovement();
		break;
	default:
		assert(false && "An unaccounted for state has been added to the camera state enum. Remove it or handle it.");
	}
}

void vulkan::Camera::setState(State state) noexcept {
	_state = state;
	switch (_state) {
	case State::Still:
		break;
	case State::FreeCAM:
		break;
	case State::Orbit:
		break;
	default:
		assert(false && "An unaccounted for state has been added to the camera state enum. Remove it or handle it.");
	}
}

void Camera::setPerspectiveProjection(float fovy, float aspect, float near, float far) {
	_projectionMatrix = glm::mat4{ 1.0f };
	_projectionMatrix[0][0] = 1.f / (aspect * tan(fovy / 2.f));
	_projectionMatrix[1][1] = 1.f / (tan(fovy / 2.f));
	_projectionMatrix[2][2] = far / (far - near);
	_projectionMatrix[2][3] = 1.f;
	_projectionMatrix[3][2] = -(far * near) / (far - near);
}

void Camera::createViewMatrix(const glm::vec3& w, const glm::vec3& u, const glm::vec3& v) {
	_viewMatrix = glm::mat4{ 1.f };
	_viewMatrix[0][0] = u.x;
	_viewMatrix[1][0] = u.y;
	_viewMatrix[2][0] = u.z;
	_viewMatrix[0][1] = v.x;
	_viewMatrix[1][1] = v.y;
	_viewMatrix[2][1] = v.z;
	_viewMatrix[0][2] = w.x;
	_viewMatrix[1][2] = w.y;
	_viewMatrix[2][2] = w.z;
	_viewMatrix[3][1] = -glm::dot(v, _position);
	_viewMatrix[3][0] = -glm::dot(u, _position);
	_viewMatrix[3][2] = -glm::dot(w, _position);
}

void Camera::updateViewMatrix() {
	const glm::vec3 u = glm::vec3(1.f - 2.f * (_orientation.y * _orientation.y + _orientation.z * _orientation.z),
		2.f * (_orientation.x * _orientation.y + _orientation.z * _orientation.w),
		2.f * (_orientation.x * _orientation.z - _orientation.y * _orientation.w));

	const glm::vec3 v = glm::vec3(2.f * (_orientation.x * _orientation.y - _orientation.z * _orientation.w),
		1.f - 2.f * (_orientation.x * _orientation.x + _orientation.z * _orientation.z),
		2.f * (_orientation.y * _orientation.z + _orientation.x * _orientation.w));

	const glm::vec3 w = glm::vec3(2.f * (_orientation.x * _orientation.z + _orientation.y * _orientation.w),
		2.f * (_orientation.y * _orientation.z - _orientation.x * _orientation.w),
		1.f - 2.f * (_orientation.x * _orientation.x + _orientation.y * _orientation.y));

	createViewMatrix(w, u, v);
}

void Camera::freeCAMMovement() {
	glm::vec3 forwardDir = glm::rotate(_orientation, glm::vec3(0.0, 0.0, 1.0));
	glm::vec3 rightDir = glm::rotate(_orientation, glm::vec3(-1.0, 0.0, 0.0));
	glm::vec3 upDir = glm::rotate(_orientation, glm::vec3(0.0, -1.0, 0.0));

	glm::vec3 moveDir{ 0.0 };
	moveDir += (float)ImGui::IsKeyDown(settings::moveForward) * forwardDir;
	moveDir -= (float)ImGui::IsKeyDown(settings::moveBackwards) * forwardDir;
	moveDir += (float)ImGui::IsKeyDown(settings::moveLeft) * rightDir;
	moveDir -= (float)ImGui::IsKeyDown(settings::moveRight) * rightDir;
	moveDir += (float)ImGui::IsKeyDown(settings::moveUp) * upDir;
	moveDir -= (float)ImGui::IsKeyDown(settings::moveDown) * upDir;

	if (moveDir != glm::vec3(0.0)) {
		_position += float(_moveSpeed * ::App::getDeltaTime()) * glm::normalize(moveDir);
	}

	glm::vec3 rotation{ 0.0 };
	glm::vec3 upRotate = glm::vec3(1.0, 0.0, 0.0);
	glm::vec3 rightRotate = glm::vec3(0.0, 1.0, 0.0);
	glm::vec3 rollRotate = glm::vec3(0.0, 0.0, -1.0);

	rotation += (float)ImGui::IsKeyDown(settings::rotateRight) * rightRotate;
	rotation -= (float)ImGui::IsKeyDown(settings::rotateLeft) * rightRotate;
	rotation += (float)ImGui::IsKeyDown(settings::rotateUp) * upRotate;
	rotation -= (float)ImGui::IsKeyDown(settings::rotateDown) * upRotate;
	rotation += (float)ImGui::IsKeyDown(settings::rollLeft) * rollRotate;
	rotation -= (float)ImGui::IsKeyDown(settings::rollRight) * rollRotate;

	float angle = glm::length(rotation) * ::App::getDeltaTime();
	if (angle > 1e-6f) {
		glm::vec3 axis = glm::normalize(rotation);
		glm::quat dq = glm::angleAxis(angle, axis);
		_orientation = glm::normalize(_orientation * dq);
	}

	rotation = glm::vec3(0.0);
	upRotate = glm::vec3(-1.0, 0.0, 0.0) * (float)_mouseSensitivity;
	rightRotate = glm::vec3(0.0, 1.0, 0.0) * (float)_mouseSensitivity;

	rotation += rightRotate * (float)InputEventHandler::mouseDelta.x;
	rotation += upRotate * (float)InputEventHandler::mouseDelta.y;

	angle = glm::length(rotation) * ::App::getDeltaTime();
	if (angle > 1e-6f) {
		glm::vec3 axis = glm::normalize(rotation);
		glm::quat dq = glm::angleAxis(angle, axis);
		_orientation = glm::normalize(_orientation * dq);
	}

	updateViewMatrix();
}

void Camera::lookAtMovement() {
	double dt = ::App::getDeltaTime();

	double zoomInput = 0.0;

	zoomInput -= InputEventHandler::mouseScrollWheel;

	if (zoomInput != 0.0) {
		double factor = std::exp(zoomInput * _zoomSpeed * dt);
		_radius *= factor;
		_radius = std::max(_radius, 0.001);
	}

	double yawInput = 0.0;
	double pitchInput = 0.0;

	if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))
		yawInput -= 1.0;
	if (ImGui::IsKeyDown(ImGuiKey_RightArrow))
		yawInput += 1.0;
	if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
		pitchInput += 1.0;
	if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
		pitchInput -= 1.0;

	_yaw += yawInput * _mouseSensitivity * dt;
	_pitch += pitchInput * _mouseSensitivity * dt;



	_yaw += (float)InputEventHandler::mouseDelta.x * _mouseSensitivity * dt;
	_pitch -= (float)InputEventHandler::mouseDelta.y * _mouseSensitivity * dt;

	constexpr double pitchLimit = glm::radians(89.0);
	_pitch = glm::clamp(_pitch, -pitchLimit, pitchLimit);

	glm::vec3 target = glm::vec3(0.0);

	glm::vec3 offset;
	offset.x = _radius * std::cos(_pitch) * std::sin(_yaw);
	offset.y = _radius * std::sin(_pitch);
	offset.z = _radius * std::cos(_pitch) * std::cos(_yaw);

	_position = target + offset;

	glm::mat4 view = glm::lookAt(_position, target, glm::vec3(0.0f, 1.0f, 0.0f));

	glm::mat3 rot = glm::mat3(glm::inverse(view));
	_orientation = glm::normalize(glm::quat_cast(rot));

	updateViewMatrix();
}

glm::mat4 vulkan::getViewMatrix(glm::vec3 position, glm::quat orientation) noexcept {
	const glm::vec3 u = glm::vec3(1.f - 2.f * (orientation.y * orientation.y + orientation.z * orientation.z),
		2.f * (orientation.x * orientation.y + orientation.z * orientation.w),
		2.f * (orientation.x * orientation.z - orientation.y * orientation.w));

	const glm::vec3 v = glm::vec3(2.f * (orientation.x * orientation.y - orientation.z * orientation.w),
		1.f - 2.f * (orientation.x * orientation.x + orientation.z * orientation.z),
		2.f * (orientation.y * orientation.z + orientation.x * orientation.w));

	const glm::vec3 w = glm::vec3(2.f * (orientation.x * orientation.z + orientation.y * orientation.w),
		2.f * (orientation.y * orientation.z - orientation.x * orientation.w),
		1.f - 2.f * (orientation.x * orientation.x + orientation.y * orientation.y));

	glm::mat4 viewMatrix{ 1.f };
	viewMatrix[0][0] = u.x;
	viewMatrix[1][0] = u.y;
	viewMatrix[2][0] = u.z;
	viewMatrix[0][1] = v.x;
	viewMatrix[1][1] = v.y;
	viewMatrix[2][1] = v.z;
	viewMatrix[0][2] = w.x;
	viewMatrix[1][2] = w.y;
	viewMatrix[2][2] = w.z;
	viewMatrix[3][1] = -glm::dot(v, position);
	viewMatrix[3][0] = -glm::dot(u, position);
	viewMatrix[3][2] = -glm::dot(w, position);
	return viewMatrix;
}