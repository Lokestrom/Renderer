#include "gameObject.hpp"

#include "Runtime.hpp"
#include "VulkanApp.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

namespace renderer {

namespace {

#ifdef _DEBUG
bool isGPUIdle = false;
#define setGPUIdle(value) isGPUIdle = value
#else
#define setGPUIdle(value)
#endif


void waitForGPU() noexcept {
	setGPUIdle(true);
	try {
		App::waitIdle();
	}
	catch (...) {
		Runtime::log(
			LogLevel::warning,
			"Failed to wait idle for device when removing game objects");
	}
}
}

GameObject::GameObject(
	ModelCache::ID&& model,
	glm::vec3 objectPosition,
	glm::quat objectOrientation,
	glm::vec3 objectScale,
	glm::vec3 modelPosition,
	glm::quat modelOrientation,
	glm::vec3 modelScale) noexcept
	: position(objectPosition)
	, orientation(objectOrientation)
	, scale(objectScale)
	, _model(model)
	, _modelTransform(
		glm::translate(glm::mat4(1.0f), modelPosition) *
		glm::toMat4(modelOrientation) *
		glm::scale(glm::mat4(1.0f), modelScale)) {
	assert(_model != 0 && "GameObject must adopt a valid model reference");
}

GameObject::~GameObject() noexcept {
	if (_model != 0) {
		ModelCache::unloadModel(_model);
	}
}

GameObject::GameObject(const GameObject& other)
	: position(other.position)
	, orientation(other.orientation)
	, scale(other.scale)
	, _model(other._model)
	, _modelTransform(other._modelTransform) {
	if (_model != 0) {
		ModelCache::addModelRefrence(_model);
	}
}

GameObject& GameObject::operator=(const GameObject& other) {
	if (this == &other) {
		return *this;
	}
	if (other._model != 0) {
		ModelCache::addModelRefrence(other._model);
	}
	if (_model != 0) {
		ModelCache::unloadModel(_model);
	}
	position = other.position;
	orientation = other.orientation;
	scale = other.scale;
	_model = other._model;
	_modelTransform = other._modelTransform;
	return *this;
}

GameObject::GameObject(GameObject&& other) noexcept
	: position(std::move(other.position))
	, orientation(std::move(other.orientation))
	, scale(std::move(other.scale))
	, _model(std::exchange(other._model, 0))
	, _modelTransform(std::move(other._modelTransform)) {
}

GameObject& GameObject::operator=(GameObject&& other) noexcept {
	if (this == &other) {
		return *this;
	}
	if (_model != 0) {
		ModelCache::unloadModel(_model);
	}
	position = std::move(other.position);
	orientation = std::move(other.orientation);
	scale = std::move(other.scale);
	_model = std::exchange(other._model, 0);
	_modelTransform = std::move(other._modelTransform);
	return *this;
}

ID GameObjectContainer::Add(GameObject object, bool isStatic) {
	assert(object.getModel() != 0 && "Model can't have a model ID of 0");
	static std::atomic<ID> currID = 1;
	const ID id = currID.fetch_add(1);
	bool objectAdded = false;
	std::lock_guard<std::mutex> lock(mutex);

	try {
		assert(!idMappings.contains(id) && "Generated duplicate game object ID");
		assert(!reverseIdMappings.contains(gameObjects.size()) && "Game object index already has a reverse mapping");
		idMappings.insert({id, gameObjects.size()});
		reverseIdMappings.insert({gameObjects.size(), id});
		gameObjects.push_back(std::move(object));
		objectAdded = true;

		if (isStatic) {
			auto [chunk, insertedChunk] = staticGameObjects.try_emplace(glm::ivec2{
				std::floor(object.position.x / static_cast<float>(chunkSize)),
				std::floor(object.position.z / static_cast<float>(chunkSize)) });
			try {
				chunk->second.push_back(id);
			}
			catch (...) {
				if (insertedChunk) {
					staticGameObjects.erase(chunk);
				}
				throw;
			}
		}
		else {
			dynamicGameObjects.push_back(id);
		}
		return id;
	}
	catch (...) {
		if (objectAdded) {
			gameObjects.pop_back();
		}
		reverseIdMappings.erase(gameObjects.size());
		idMappings.erase(id);
		throw;
	}
}

void GameObjectContainer::remove(ID id) noexcept {
	waitForGPU();
	_remove(id);
	setGPUIdle(false);
}

void GameObjectContainer::remove(const std::vector<ID>& ids) noexcept {
	if (ids.empty())
		return;

	waitForGPU();
	for (ID id : ids) {
		_remove(id);
	}
	setGPUIdle(false);
}

void GameObjectContainer::removeWithInvalids(const std::vector<ID>& ids) noexcept {
	if (ids.empty())
		return;
	waitForGPU();
	for (ID id : ids) {
		if (id != 0)
			_remove(id);
	}
	setGPUIdle(false);
}


GameObject& GameObjectContainer::get(ID id) noexcept {
	assert(idMappings.contains(id) && "There is no gameobject with this id");
	const size_t index = idMappings.find(id)->second;
	assert(gameObjects.size() > index && "The index is out of range");
	return gameObjects[index];
}

const std::vector<ID>& GameObjectContainer::getDynamicGameObjects() noexcept {
	return dynamicGameObjects;
}

std::array<GameObjectContainer::StaticChunk, 9> GameObjectContainer::getStaticGameObjectChunks(const glm::vec2& position) noexcept {
	return getStaticGameObjectChunks(getStaticChunkCoords(position));
}

std::array<GameObjectContainer::StaticChunk, 9> GameObjectContainer::getStaticGameObjectChunks(const glm::ivec2& centerChunk) noexcept {
	std::array<StaticChunk, 9> chunks{};
	size_t chunkIndex = 0;
	for (int x = -1; x != 2; x++) {
		for (int z = -1; z != 2; z++) {
			const glm::ivec2 coordinates = centerChunk + glm::ivec2{ x, z };
			if (const auto chunk = staticGameObjects.find(coordinates); chunk != staticGameObjects.end()) {
				chunks[chunkIndex] = StaticChunk{
					.offset = { x, z },
					.objects = &chunk->second
				};
				++chunkIndex;
			}
		}
	}
	return chunks;
}

std::vector<GameObjectContainer::StaticChunk> GameObjectContainer::getStaticGameObjectChunks(
	const glm::vec2& minimum,
	const glm::vec2& maximum) {
	assert(glm::all(glm::lessThanEqual(minimum, maximum)) && "Static chunk bounds must be ordered");
	const glm::ivec2 minimumChunk = getStaticChunkCoords(minimum);
	const glm::ivec2 maximumChunk = getStaticChunkCoords(maximum);

	std::vector<StaticChunk> chunks;
	chunks.reserve(staticGameObjects.size());
	for (const auto& [coordinates, objects] : staticGameObjects) {
		if (glm::all(glm::greaterThanEqual(coordinates, minimumChunk)) &&
			glm::all(glm::lessThanEqual(coordinates, maximumChunk))) {
			chunks.push_back({
				.offset = coordinates,
				.objects = &objects
			});
		}
	}
	return chunks;
}

glm::ivec2 GameObjectContainer::getStaticChunkCoords(const glm::vec2& position) noexcept {
	return {
		std::floor(position.x / static_cast<float>(chunkSize)),
		std::floor(position.y / static_cast<float>(chunkSize))
	};
}

void GameObjectContainer::_remove(ID id) noexcept {
	assert(isGPUIdle && "GPU must be idle when removing game objects");
	assert(idMappings.contains(id) && "ID not found in idMappings");

	const size_t removeIndex = idMappings.find(id)->second;
	const size_t lastIndex = gameObjects.size() - 1;

	if (removeIndex != lastIndex) {
		assert(reverseIdMappings.contains(lastIndex) && "Every game object index must have a reverse ID mapping");
		assert(reverseIdMappings.contains(removeIndex) && "The removed game object index must have a reverse mapping");
		assert(idMappings.contains(reverseIdMappings.find(lastIndex)->second) && "Every reverse ID mapping must have a forward mapping");

		const ID movedID = reverseIdMappings.find(lastIndex)->second;
		const auto removedReverseMapping = reverseIdMappings.find(removeIndex);
		gameObjects[removeIndex] = std::move(gameObjects[lastIndex]);
		idMappings.find(movedID)->second = removeIndex;
		removedReverseMapping->second = movedID;
	}

	gameObjects.pop_back();
	idMappings.erase(id);
	reverseIdMappings.erase(lastIndex);

	auto itDynamic = std::ranges::find(dynamicGameObjects, id);
	if (itDynamic != dynamicGameObjects.end())
		dynamicGameObjects.erase(itDynamic);
	else
		for (auto& [key, val] : staticGameObjects) {
			auto itStatic = std::ranges::find(val, id);
			if (itStatic != val.end()) {
				val.erase(itStatic);
				break;
			}
		}
}

}
