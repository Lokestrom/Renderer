#include "gameObject.hpp"

namespace vulkan {

ID GameObjectContainer::Add(GameObject&& object) {
	assert(object.model != 0 && "Model can't have a model ID of 0");
	static std::atomic<ID> currID = 1;
	ID id = currID.fetch_add(1);

	std::lock_guard<std::mutex> lock(mutex);
	idMappings[currID] = gameObjects.size();
	gameObjects.push_back(std::move(object));

	return currID;
}

void GameObjectContainer::Remove(ID id) noexcept {
	size_t idIndex = idMappings[id];
	for (auto& [key, val] : idMappings) {
		if (val > idIndex) {
			val--;
		}
	}
	idMappings.erase(id);
	ModelCache::unloadModel(gameObjects[idIndex].model);
	gameObjects.erase(gameObjects.begin() + idIndex);
}

GameObject& GameObjectContainer::get(ID id) noexcept {
	return gameObjects[idMappings[id]];
}

const std::vector<GameObject>& GameObjectContainer::getObjects() noexcept {
	return gameObjects;
}

}
