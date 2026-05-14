#pragma once

#include "VulkanApp.hpp"
#include "texture.hpp"

#include <filesystem>
#include <array>
#include <unordered_map>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace vulkan {

// creation must be thread safe
class Model3D {
	friend class ModelCache;

public:
	struct Vertex {
		glm::vec3 position;
		glm::vec3 color;
		glm::vec3 normal;
		glm::vec2 uv;

		static std::array<vk::VertexInputBindingDescription, 1> bindingDescriptions() noexcept;
		static std::array<vk::VertexInputAttributeDescription, 4> attributeDescriptions() noexcept;
	};


	struct CreationTransform {
		glm::vec3 position;
		glm::vec3 scale = glm::vec3{ 1.0 };
		glm::quat rotation;
		glm::vec4 color;

		bool operator== (const CreationTransform& other) const {
			return position == other.position &&
				scale == other.scale &&
				rotation == other.rotation &&
				color == other.color;
		}
	};

	Model3D()
		: vertexCount(0), vertexBuffer(nullptr), vertexMemory(nullptr) {}
	Model3D(std::filesystem::path file, CreationTransform transform);

	Model3D(Model3D&) = delete;
	Model3D& operator=(Model3D&) = delete;

	Model3D(Model3D&&) noexcept;
	Model3D& operator=(Model3D&&) noexcept;

	~Model3D();

	void draw(vk::CommandBuffer cmd, vk::PipelineLayout layout) const noexcept;

private:
	void destroy();

private:
	uint32_t vertexCount;
	vk::Buffer vertexBuffer;
	vk::DeviceMemory vertexMemory;

	std::vector<std::pair<size_t, TextureCache::ID>> _textureIndecies = {};

	// TODO: implement index buffers
	// uint32_t indexCount;
	// vk::DeviceMemory indexMemory;
	// vk::Buffer indexBuffer;
};

// TODO: batch game objects that use the same model to reduce draw calls, but for now this is good enough
// should probably drop the creation transform as it is just a shortcut
// also this shit aint RAII, instead of using id use handles that call unload

class ModelCache {
public:
	// 0 is reserved for no model
	using ID = size_t;

	static Model3D& getModel(ID id);

	[[nodiscard]]
	static ID loadModel(std::filesystem::path file, Model3D::CreationTransform transform);
	static void unloadModel(ID id);

private:
	struct ModelKeyHash {
		size_t operator()(const std::pair<std::filesystem::path, Model3D::CreationTransform>& key) const {
			std::string combined = key.first.string() + 
				std::to_string(key.second.position.x) + std::to_string(key.second.position.y) + std::to_string(key.second.position.z) +
				std::to_string(key.second.scale.x) + std::to_string(key.second.scale.y) + std::to_string(key.second.scale.z) +
				std::to_string(key.second.rotation.x) + std::to_string(key.second.rotation.y) + std::to_string(key.second.rotation.z) + std::to_string(key.second.rotation.w) +
				std::to_string(key.second.color.r) + std::to_string(key.second.color.g) + std::to_string(key.second.color.b) + std::to_string(key.second.color.a);
			return std::hash<std::string>{}(combined);
		}
	};

	static inline std::unordered_map<std::pair<std::filesystem::path, Model3D::CreationTransform>, ID, ModelKeyHash> _idMap;
	static inline std::unordered_map<ID, size_t> _refCounts;
	static inline std::unordered_map<ID, Model3D> _cache;
	static inline std::mutex _mutex;
};
}