#include "gameObject.hpp"

namespace vulkan {

ID GameObjectContainer::Add(GameObject&& object) {
	static ID currID = 0;
	currID++;

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

	gameObjects.erase(gameObjects.begin() + idIndex);
}

GameObject& GameObjectContainer::get(ID id) noexcept {
	return gameObjects[idMappings[id]];
}

const std::vector<GameObject>& GameObjectContainer::getObjects() noexcept {
	return gameObjects;
}

}
