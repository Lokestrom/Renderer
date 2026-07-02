#include "Camera.hpp"

#include "../App.hpp"
#include "../Settings.hpp"
#include "../Input/InputEventHandler.hpp"
#include "../gui/settingsGui.hpp"
#include "../SettingNames.hpp"

using namespace vulkan;

void vulkan::createCameraSettings() {
	auto& cameraSettings = ::App::settings.newCategory(settingNames::categories::camera);

	cameraSettings.emplace<settings::ValueWithRange<double>>(settingNames::camera::fieldOfView, 70.0,
		settings::ValueWithRange<double>::setFunctionT(gui::slider), 30.0, 120.0,
		"Vertical field of view in degrees.");
	cameraSettings.emplace<settings::ValueWithRange<double>>(settingNames::camera::moveSpeed, 20.0,
		settings::ValueWithRange<double>::setFunctionT(gui::slider), 0.1, 200.0,
		"Free-camera movement speed in world units per second.");
	cameraSettings.emplace<settings::ValueWithRange<double>>(settingNames::camera::keyboardRotationSpeed, 1.0,
		settings::ValueWithRange<double>::setFunctionT(gui::slider), 0.1, 10.0,
		"Multiplier applied to camera rotation from keyboard input.");
	cameraSettings.emplace<settings::ValueWithRange<double>>(settingNames::camera::mouseSensitivity, 0.01,
		settings::ValueWithRange<double>::setFunctionT(gui::slider), 0.001, 1.0);
	cameraSettings.emplace<settings::ValueWithRange<double>>(settingNames::camera::zoomSpeed, 0.1,
		settings::ValueWithRange<double>::setFunctionT(gui::slider), 0.01, 1.0);
	cameraSettings.emplace<settings::ValueWithRange<double>>(settingNames::camera::minimumOrbitDistance, 0.001,
		settings::ValueWithRange<double>::setFunctionT(gui::slider), 0.001, 100.0,
		"Closest distance allowed between the orbit camera and its target.");
	cameraSettings.emplace<settings::ValueWithRange<double>>(settingNames::camera::maximumOrbitDistance, 10000.0,
		settings::ValueWithRange<double>::setFunctionT(gui::slider), 1.0, 10000.0,
		"Farthest distance allowed between the orbit camera and its target.");

	using KeyValue = settings::Value<ImGuiKey>;
	auto& cameraKeyBinds = ::App::settings.get(settingNames::categories::keyBindings)
		.addSubCategory(settingNames::categories::camera);
	cameraKeyBinds.emplace<KeyValue>(settingNames::cameraKeys::moveForward, ImGuiKey_W, KeyValue::setFunctionT(gui::keyBindButton));
	cameraKeyBinds.emplace<KeyValue>(settingNames::cameraKeys::moveBackwards, ImGuiKey_S, KeyValue::setFunctionT(gui::keyBindButton));
	cameraKeyBinds.emplace<KeyValue>(settingNames::cameraKeys::moveLeft, ImGuiKey_A, KeyValue::setFunctionT(gui::keyBindButton));
	cameraKeyBinds.emplace<KeyValue>(settingNames::cameraKeys::moveRight, ImGuiKey_D, KeyValue::setFunctionT(gui::keyBindButton));
	cameraKeyBinds.emplace<KeyValue>(settingNames::cameraKeys::moveUp, ImGuiKey_Space, KeyValue::setFunctionT(gui::keyBindButton));
	cameraKeyBinds.emplace<KeyValue>(settingNames::cameraKeys::moveDown, ImGuiKey_LeftShift, KeyValue::setFunctionT(gui::keyBindButton));
	cameraKeyBinds.emplace<KeyValue>(settingNames::cameraKeys::rotateLeft, ImGuiKey_LeftArrow, KeyValue::setFunctionT(gui::keyBindButton));
	cameraKeyBinds.emplace<KeyValue>(settingNames::cameraKeys::rotateRight, ImGuiKey_RightArrow, KeyValue::setFunctionT(gui::keyBindButton));
	cameraKeyBinds.emplace<KeyValue>(settingNames::cameraKeys::rotateUp, ImGuiKey_UpArrow, KeyValue::setFunctionT(gui::keyBindButton));
	cameraKeyBinds.emplace<KeyValue>(settingNames::cameraKeys::rotateDown, ImGuiKey_DownArrow, KeyValue::setFunctionT(gui::keyBindButton));
	cameraKeyBinds.emplace<KeyValue>(settingNames::cameraKeys::rollLeft, ImGuiKey_Q, KeyValue::setFunctionT(gui::keyBindButton));
	cameraKeyBinds.emplace<KeyValue>(settingNames::cameraKeys::rollRight, ImGuiKey_E, KeyValue::setFunctionT(gui::keyBindButton));
	cameraKeyBinds.emplace<KeyValue>(settingNames::cameraKeys::freeCamera, ImGuiKey_F, KeyValue::setFunctionT(gui::keyBindButton));
	cameraKeyBinds.emplace<KeyValue>(settingNames::cameraKeys::orbitCamera, ImGuiKey_T, KeyValue::setFunctionT(gui::keyBindButton));
}

Camera::Camera() noexcept
	: _projectionMatrix(1.0f)
	, _viewMatrix(1.0f)
	, _position(0.0f, 0.0f, 0.0f)
	, _orientation(1.0f, 0.0f, 0.0f, 0.0f)
	, _fieldOfView(::App::settings.get(settingNames::categories::camera).get<double>(settingNames::camera::fieldOfView).getHandle())
	, _moveSpeed(::App::settings.get(settingNames::categories::camera).get<double>(settingNames::camera::moveSpeed).getHandle())
	, _keyboardRotationSpeed(::App::settings.get(settingNames::categories::camera).get<double>(settingNames::camera::keyboardRotationSpeed).getHandle())
	, _mouseSensitivity(::App::settings.get(settingNames::categories::camera).get<double>(settingNames::camera::mouseSensitivity).getHandle())
	, _minOrbitDistance(::App::settings.get(settingNames::categories::camera).get<double>(settingNames::camera::minimumOrbitDistance).getHandle())
	, _maxOrbitDistance(::App::settings.get(settingNames::categories::camera).get<double>(settingNames::camera::maximumOrbitDistance).getHandle())
	, _zoomSpeed(::App::settings.get(settingNames::categories::camera).get<double>(settingNames::camera::zoomSpeed).getHandle())
	, _moveForward(::App::settings.get(settingNames::categories::keyBindings).getSubCategory(settingNames::categories::camera).get<ImGuiKey>(settingNames::cameraKeys::moveForward).getHandle())
	, _moveBackwards(::App::settings.get(settingNames::categories::keyBindings).getSubCategory(settingNames::categories::camera).get<ImGuiKey>(settingNames::cameraKeys::moveBackwards).getHandle())
	, _moveLeft(::App::settings.get(settingNames::categories::keyBindings).getSubCategory(settingNames::categories::camera).get<ImGuiKey>(settingNames::cameraKeys::moveLeft).getHandle())
	, _moveRight(::App::settings.get(settingNames::categories::keyBindings).getSubCategory(settingNames::categories::camera).get<ImGuiKey>(settingNames::cameraKeys::moveRight).getHandle())
	, _moveUp(::App::settings.get(settingNames::categories::keyBindings).getSubCategory(settingNames::categories::camera).get<ImGuiKey>(settingNames::cameraKeys::moveUp).getHandle())
	, _moveDown(::App::settings.get(settingNames::categories::keyBindings).getSubCategory(settingNames::categories::camera).get<ImGuiKey>(settingNames::cameraKeys::moveDown).getHandle())
	, _rotateLeft(::App::settings.get(settingNames::categories::keyBindings).getSubCategory(settingNames::categories::camera).get<ImGuiKey>(settingNames::cameraKeys::rotateLeft).getHandle())
	, _rotateRight(::App::settings.get(settingNames::categories::keyBindings).getSubCategory(settingNames::categories::camera).get<ImGuiKey>(settingNames::cameraKeys::rotateRight).getHandle())
	, _rotateUp(::App::settings.get(settingNames::categories::keyBindings).getSubCategory(settingNames::categories::camera).get<ImGuiKey>(settingNames::cameraKeys::rotateUp).getHandle())
	, _rotateDown(::App::settings.get(settingNames::categories::keyBindings).getSubCategory(settingNames::categories::camera).get<ImGuiKey>(settingNames::cameraKeys::rotateDown).getHandle())
	, _rollLeft(::App::settings.get(settingNames::categories::keyBindings).getSubCategory(settingNames::categories::camera).get<ImGuiKey>(settingNames::cameraKeys::rollLeft).getHandle())
	, _rollRight(::App::settings.get(settingNames::categories::keyBindings).getSubCategory(settingNames::categories::camera).get<ImGuiKey>(settingNames::cameraKeys::rollRight).getHandle()) {
	setPerspectiveProjection(glm::radians(static_cast<float>(_fieldOfView.get())),
		static_cast<float>(::App::width) / static_cast<float>(::App::height), 0.01f, 10000.0f);
	updateViewMatrix();
}

void Camera::update() {
	if (::App::width > 0 && ::App::height > 0) {
		setPerspectiveProjection(glm::radians(static_cast<float>(_fieldOfView.get())),
			static_cast<float>(::App::width) / static_cast<float>(::App::height), 0.01f, 10000.0f);
	}

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
	moveDir += (float)ImGui::IsKeyDown(_moveForward) * forwardDir;
	moveDir -= (float)ImGui::IsKeyDown(_moveBackwards) * forwardDir;
	moveDir += (float)ImGui::IsKeyDown(_moveLeft) * rightDir;
	moveDir -= (float)ImGui::IsKeyDown(_moveRight) * rightDir;
	moveDir += (float)ImGui::IsKeyDown(_moveUp) * upDir;
	moveDir -= (float)ImGui::IsKeyDown(_moveDown) * upDir;

	if (moveDir != glm::vec3(0.0)) {
		_position += float(_moveSpeed * ::App::getDeltaTime()) * glm::normalize(moveDir);
	}

	glm::vec3 rotation{ 0.0 };
	glm::vec3 upRotate = glm::vec3(1.0, 0.0, 0.0);
	glm::vec3 rightRotate = glm::vec3(0.0, 1.0, 0.0);
	glm::vec3 rollRotate = glm::vec3(0.0, 0.0, -1.0);

	rotation += (float)ImGui::IsKeyDown(_rotateRight) * rightRotate;
	rotation -= (float)ImGui::IsKeyDown(_rotateLeft) * rightRotate;
	rotation += (float)ImGui::IsKeyDown(_rotateUp) * upRotate;
	rotation -= (float)ImGui::IsKeyDown(_rotateDown) * upRotate;
	rotation += (float)ImGui::IsKeyDown(_rollLeft) * rollRotate;
	rotation -= (float)ImGui::IsKeyDown(_rollRight) * rollRotate;

	rotation *= static_cast<float>(_keyboardRotationSpeed.get());
	rotation -= upRotate * (float)_mouseSensitivity * (float)InputEventHandler::mouseDelta.y * 10.f;
	rotation += rightRotate * (float)_mouseSensitivity * (float)InputEventHandler::mouseDelta.x * 10.f;

	float angle = glm::length(rotation) * ::App::getDeltaTime();
	if (angle > 1e-6f) {
		glm::vec3 axis = glm::normalize(rotation);
		glm::quat dq = glm::angleAxis(angle, axis);
		_orientation = glm::normalize(_orientation * dq);
	}

	updateViewMatrix();
}

void Camera::lookAtMovement() {
	double zoomInput = 0.0;

	zoomInput -= InputEventHandler::mouseScrollWheel;

	if (zoomInput != 0.0) {
		double factor = std::exp(zoomInput * _zoomSpeed);
		_radius *= factor;
	}

	const double minimumDistance = std::min(_minOrbitDistance.get(), _maxOrbitDistance.get());
	const double maximumDistance = std::max(_minOrbitDistance.get(), _maxOrbitDistance.get());
	_radius = glm::clamp(_radius, minimumDistance, maximumDistance);

	double yawInput = 0.0;
	double pitchInput = 0.0;

	if (ImGui::IsKeyDown(_rotateLeft))
		yawInput -= 1.0;
	if (ImGui::IsKeyDown(_rotateRight))
		yawInput += 1.0;
	if (ImGui::IsKeyDown(_rotateUp))
		pitchInput += 1.0;
	if (ImGui::IsKeyDown(_rotateDown))
		pitchInput -= 1.0;

	_yaw += yawInput * _mouseSensitivity * _keyboardRotationSpeed;
	_pitch += pitchInput * _mouseSensitivity * _keyboardRotationSpeed;

	_yaw += (float)InputEventHandler::mouseDelta.x * _mouseSensitivity;
	_pitch -= (float)InputEventHandler::mouseDelta.y * _mouseSensitivity;

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
