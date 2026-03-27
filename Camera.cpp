#include "Camera.hpp"

#include "../App.hpp"
#include "../Settings.hpp"

static glm::quat getStepQuaternion(const glm::quat& orientation, const glm::quat& desiredOrientation, int n, float maxRotationSpeed) noexcept {
	glm::quat step = (desiredOrientation - orientation) / (float)n;
	float stepMagnitude = sqrt(step.x * step.x + step.y * step.y + step.z * step.z);
	if (stepMagnitude > maxRotationSpeed) {
		float scale = maxRotationSpeed / stepMagnitude;
		step.x *= scale;
		step.y *= scale;
		step.z *= scale;
	}
	return step;
}

Camera::Camera() noexcept
	: projectionMatrix(1.0f),
	viewMatrix(1.0f),
	position(0.0f, 0.0f, 0.0f), 
	orientation(1.0f, 0.0f, 0.0f, 0.0f) 
{
	setPerspectiveProjection(glm::radians(70.0f), 4.0f / 3.0f, 0.1f, 100.0f);
	updateViewMatrix();
}

void Camera::update() {
	if (!cameraMovementEnabled)
		return;

	glm::vec3 forwardDir = glm::rotate(orientation, glm::vec3(0.0, 0.0, 1.0));
	glm::vec3 rightDir = glm::rotate(orientation, glm::vec3(-1.0, 0.0, 0.0));
	glm::vec3 upDir = glm::rotate(orientation, glm::vec3(0.0, -1.0, 0.0));

	glm::vec3 moveDir{ 0.0 };
	moveDir += (float)ImGui::IsKeyDown(settings::moveForward) * forwardDir;
	moveDir -= (float)ImGui::IsKeyDown(settings::moveBackwards) * forwardDir;
	moveDir += (float)ImGui::IsKeyDown(settings::moveLeft) * rightDir;
	moveDir -= (float)ImGui::IsKeyDown(settings::moveRight) * rightDir;
	moveDir += (float)ImGui::IsKeyDown(settings::moveUp) * upDir;
	moveDir -= (float)ImGui::IsKeyDown(settings::moveDown) * upDir;
	 
	if (moveDir != glm::vec3(0.0)) {
		position += _moveSpeed * (float)App::getDeltaTime() * glm::normalize(moveDir);
	}

	glm::vec3 rotation{ 0.0 };
	glm::vec3 upRotate = glm::vec3(1.0,0.0,0.0);
	glm::vec3 rightRotate = glm::vec3(0.0, 1.0, 0.0);
	glm::vec3 rollRotate = glm::vec3(0.0, 0.0, -1.0);

	rotation += (float)ImGui::IsKeyDown(settings::rotateRight) * rightRotate;
	rotation -= (float)ImGui::IsKeyDown(settings::rotateLeft) * rightRotate;
	rotation += (float)ImGui::IsKeyDown(settings::rotateUp) * upRotate;
	rotation -= (float)ImGui::IsKeyDown(settings::rotateDown) * upRotate;
	rotation += (float)ImGui::IsKeyDown(settings::rollLeft) * rollRotate;
	rotation -= (float)ImGui::IsKeyDown(settings::rollRight) * rollRotate;

	float angle = glm::length(rotation) * App::getDeltaTime();
	if (angle > 1e-6f) {
		glm::vec3 axis = glm::normalize(rotation);
		glm::quat dq = glm::angleAxis(angle, axis);
		orientation = glm::normalize(orientation * dq);
	}

	double xpos, ypos;
	glfwGetCursorPos(App::getGLFWwindow(), &xpos, &ypos);

	glm::vec2 delta = glm::vec2((xpos - _lastMousePosition.x), (ypos - _lastMousePosition.y));
	_lastMousePosition = glm::vec2(xpos, ypos);

	rotation = glm::vec3(0.0);
	upRotate = glm::vec3(-1.0, 0.0, 0.0) * _mouseSensitivity;
	rightRotate = glm::vec3(0.0, 1.0, 0.0) * _mouseSensitivity;

	rotation += rightRotate * delta.x;
	rotation += upRotate * delta.y;

	angle = glm::length(rotation) * App::getDeltaTime();
	if (angle > 1e-6f) {
		glm::vec3 axis = glm::normalize(rotation);
		glm::quat dq = glm::angleAxis(angle, axis);
		orientation = glm::normalize(orientation * dq);
	}

	updateViewMatrix();
}

void Camera::setPerspectiveProjection(float fovy, float aspect, float near, float far) {
	projectionMatrix = glm::mat4{ 1.0f };
	projectionMatrix[0][0] = 1.f / (aspect * tan(fovy / 2.f));
	projectionMatrix[1][1] = 1.f / (tan(fovy / 2.f));
	projectionMatrix[2][2] = far / (far - near);
	projectionMatrix[2][3] = 1.f;
	projectionMatrix[3][2] = -(far * near) / (far - near);
}

void Camera::createViewMatrix(const glm::vec3& w, const glm::vec3& u, const glm::vec3& v) {
	viewMatrix = glm::mat4{ 1.f };
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
}

void Camera::updateViewMatrix() {
	const glm::vec3 u = glm::vec3(1.f - 2.f * (orientation.y * orientation.y + orientation.z * orientation.z),
		2.f * (orientation.x * orientation.y + orientation.z * orientation.w),
		2.f * (orientation.x * orientation.z - orientation.y * orientation.w));

	const glm::vec3 v = glm::vec3(2.f * (orientation.x * orientation.y - orientation.z * orientation.w),
		1.f - 2.f * (orientation.x * orientation.x + orientation.z * orientation.z),
		2.f * (orientation.y * orientation.z + orientation.x * orientation.w));

	const glm::vec3 w = glm::vec3(2.f * (orientation.x * orientation.z + orientation.y * orientation.w),
		2.f * (orientation.y * orientation.z - orientation.x * orientation.w),
		1.f - 2.f * (orientation.x * orientation.x + orientation.y * orientation.y));

	createViewMatrix(w, u, v);
}

glm::mat4 getViewMatrix(glm::vec3 position, glm::quat orientation) noexcept {
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