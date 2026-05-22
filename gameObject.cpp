#include "gameObject.hpp"

namespace vulkan {

ID GameObjectContainer::Add(GameObject&& object, bool isStatic) {
	assert(object.model != 0 && "Model can't have a model ID of 0");
	static std::atomic<ID> currID = 1;
	ID id = currID.fetch_add(1);

	std::lock_guard<std::mutex> lock(mutex);
	idMappings[id] = gameObjects.size();
	reverseIdMappings[gameObjects.size()] = id;
	gameObjects.push_back(std::move(object));

	if (isStatic) {
		glm::ivec2 coords = { std::floor(object.position.x / (float)chuckSize), std::floor(object.position.y / (float)chuckSize) };
		if (!staticGameObjects.contains(coords))
			staticGameObjects.emplace();
		staticGameObjects[coords].push_back(id);
	}
	else {
		dynamicGameObjects.push_back(id);
	}

	return id;
}

void GameObjectContainer::Remove(ID id) noexcept {
	auto mappingIt = idMappings.find(id);
	assert(mappingIt != idMappings.end() && "ID not found in idMappings");

	size_t removeIndex = mappingIt->second;
	size_t lastIndex = gameObjects.size() - 1;

	ModelCache::unloadModel(gameObjects[removeIndex].model);
	if (removeIndex != lastIndex) {
		gameObjects[removeIndex] = std::move(gameObjects[lastIndex]);
		idMappings[reverseIdMappings[lastIndex]] = removeIndex;
		reverseIdMappings[removeIndex] = reverseIdMappings[lastIndex];
	}

	gameObjects.pop_back();
	idMappings.erase(id);
	reverseIdMappings.erase(lastIndex);

	auto itDynamic = std::find(dynamicGameObjects.begin(), dynamicGameObjects.end(), id);
	if (itDynamic != dynamicGameObjects.end())
		dynamicGameObjects.erase(itDynamic);
	else
		for (auto& [key, val] : staticGameObjects) {
			auto itStatic = std::find(val.begin(), val.end(), id);
			if (itStatic != val.end()) {
				val.erase(itStatic);
				break;
			}
		}
}

void GameObjectContainer::Remove(const std::vector<ID>& ids) noexcept {
	for (ID id : ids) {
		Remove(id);
	}
}

GameObject& GameObjectContainer::get(ID id) noexcept {
	return gameObjects[idMappings[id]];
}

const std::vector<ID> GameObjectContainer::getDynamicGameObjects() noexcept {
	return dynamicGameObjects;
}

const std::array<std::vector<ID>*, 9> GameObjectContainer::getStaticGameObjects(const glm::vec2& position) noexcept {
	std::array<std::vector<ID>*, 9> objects{};
	glm::ivec2 coords = { std::floor(position.x / (float)chuckSize), std::floor(position.y / (float)chuckSize) };
	short i = 0;
	for (short x = -1; x != 2; x++) {
		for (short y = -1; y != 2; y++) {
			glm::ivec2 nowCoord = coords + glm::ivec2{ x, y };
			if (staticGameObjects.contains(nowCoord)) {
				objects[i] = &staticGameObjects[nowCoord];
				i++;
			}
		}
	}
	return objects;
}

}
