#pragma once

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace renderer {

class Camera {
public:
	Camera() noexcept;

	Camera(const Camera&) = delete;
	Camera& operator=(const Camera&) = delete;

	Camera(Camera&&) noexcept = default;
	Camera& operator=(Camera&&) noexcept = default;

	[[nodiscard]] const glm::mat4& getProjection() const noexcept { return _projectionMatrix; }
	[[nodiscard]] const glm::mat4& getView() const noexcept { return _viewMatrix; }
	[[nodiscard]] const glm::vec3& getPosition() const noexcept { return _position; }
	[[nodiscard]] const glm::quat& getOrientation() const noexcept { return _orientation; }

	void setTransform(glm::vec3 position, glm::quat orientation) noexcept;
	void setPosition(glm::vec3 position) noexcept;
	void setOrientation(glm::quat orientation) noexcept;
	void translate(glm::vec3 worldDelta) noexcept;
	void rotate(glm::quat localDelta) noexcept;

	void setPerspective(float verticalFieldOfViewRadians, float aspect, float nearPlane, float farPlane) noexcept;

private:
	void updateViewMatrix() noexcept;

	glm::vec3 _position{ 0.0f };
	glm::quat _orientation{ 1.0f, 0.0f, 0.0f, 0.0f };
	glm::mat4 _projectionMatrix{ 1.0f };
	glm::mat4 _viewMatrix{ 1.0f };
};

[[nodiscard]] glm::mat4 getViewMatrix(glm::vec3 position, glm::quat orientation) noexcept;

}
