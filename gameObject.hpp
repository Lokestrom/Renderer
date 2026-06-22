#pragma once

#include "Model.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <vector>
#include <unordered_map>
#include <mutex>

namespace vulkan {

using ID = unsigned long long;

// TODO: implement one for static models with just a modelTransform

class GameObject {
public:
	glm::vec3 position;
	glm::quat orientation;
	glm::vec3 scale;

	GameObject(ModelCache::ID model,
		glm::vec3 _position,
		glm::quat _orientation,
		glm::vec3 _scale,
		glm::vec3 _modelPosition = glm::vec3(0.0),
		glm::quat _modelOrientation = glm::quat(1.,0.,0.,0.),
		glm::vec3 _modelScale = glm::vec3(1.0)
	)
		: _model(model)
		, position(_position)
		, orientation(_orientation)
		, scale(_scale)
		, _modelTransform(glm::translate(glm::mat4(1.0f), _modelPosition) *
						  glm::toMat4(_modelOrientation) *
			glm::scale(glm::mat4(1.0f), _modelScale))
	{ }

	glm::mat4 getTransformMatrix() const noexcept {
		glm::mat4 world =
			glm::translate(glm::mat4(1.0f), position) *
			glm::toMat4(orientation) *
			glm::scale(glm::mat4(1.0f), scale);

		return world * _modelTransform;

	}

	const ModelCache::ID getModel() const noexcept {
		return _model;
	}

private:
	ModelCache::ID _model;
	glm::mat4 _modelTransform;
};

class GameObjectContainer {
public:
	[[nodiscard]]
	static ID Add(GameObject&& object, bool isStatic = false);
	static void Remove(ID id)  noexcept;
	static void Remove(const std::vector<ID>& ids) noexcept;
	static void RemoveWithInvalids(const std::vector<ID>& ids) noexcept;

	static GameObject& get(ID id) noexcept;

	static const std::vector<ID> getDynamicGameObjects() noexcept;
	static const std::array<std::vector<ID>*, 9> getStaticGameObjects(const glm::vec2& position) noexcept;

private:

	constexpr static size_t chunkSize = 1000;

	// TODO: create 3 different chunk distances.
	// 1. close: with the 3 biggest mip maps on the gpu
	// 2. mid: with the 3 smallest mip maps on the gpu
	// 3. far: with no mip maps on the gpu
	// here also maybe have different lod models to and also maybe have 
	// one more and unload then form the gpu when they are far enough away
	// or even unload them from ram

	// TODO: instead of giving an vector of ids to iterate 
	// give the actual vector of game objects to iterate over, 
	// this should lead to fewer cache misses
	// needs benchmarking

	static inline std::vector<GameObject> gameObjects;
	static inline std::unordered_map<ID, size_t> idMappings;
	static inline std::unordered_map<size_t, ID> reverseIdMappings;
	
	struct glmIvec2Hash {
		size_t operator()(const glm::ivec2& vec) const {
			std::string combined = std::to_string(vec.x) + "," + std::to_string(vec.y);
			return std::hash<std::string>{}(combined);
		}
	};
	static inline std::vector<ID> dynamicGameObjects;
	static inline std::unordered_map<glm::ivec2, std::vector<ID>, glmIvec2Hash> staticGameObjects;
	static inline std::mutex mutex;
};

}