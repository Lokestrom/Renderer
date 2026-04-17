#pragma once

#include "Model.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <vector>
#include <unordered_map>

namespace vulkan {

using ID = unsigned long long;

struct GameObject {
	ModelCache::ID model;
	glm::vec3 position;
	glm::quat orientation;

	glm::mat4 getTransformMatrix() const noexcept {
		glm::mat4 translation = glm::translate(glm::mat4{ 1.f }, position);
		glm::mat4 rotation = glm::toMat4(orientation);
		return translation * rotation;
	}
};

class GameObjectContainer {
public:
	[[nodiscard]]
	static ID Add(GameObject&& object);
	static void Remove(ID id)  noexcept;

	static GameObject& get(ID id) noexcept;

	static const std::vector<GameObject>& getObjects() noexcept;

private:
	static inline std::unordered_map<ID, size_t> idMappings;
	static inline std::vector<GameObject> gameObjects;
};

}