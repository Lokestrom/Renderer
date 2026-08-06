#pragma once

#include "VulkanApp.hpp"
#include "texture.hpp"

#include <filesystem>
#include <array>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace vulkan {

// creation must be thread safe
class Model3D {
	friend class ModelCache;

public:
	struct BoundingSphere {
		glm::vec3 center{ 0.0f };
		float radius = 0.0f;
	};

	struct Vertex {
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uv;

		static std::array<vk::VertexInputBindingDescription, 1> bindingDescriptions() noexcept;
		static std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions() noexcept;
	};

	Model3D()
		: vertexCount(0), vertexMemory(nullptr), vertexBuffer(nullptr), indexCount(0), indexMemory(nullptr), indexBuffer(nullptr) {}
	Model3D(std::filesystem::path file, vk::CommandPool commandPool = nullptr);

	Model3D(Model3D&) = delete;
	Model3D& operator=(Model3D&) = delete;

	Model3D(Model3D&&) noexcept;
	Model3D& operator=(Model3D&&) noexcept;

	// TODO: make noexcept
	~Model3D() noexcept;

	void draw(vk::CommandBuffer cmd, vk::PipelineLayout layout) const noexcept;
	void drawGeometry(vk::CommandBuffer cmd) const noexcept;
	[[nodiscard]]
	const BoundingSphere& getBoundingSphere() const noexcept { return _boundingSphere; }
	[[nodiscard]]
	const std::vector<std::pair<uint32_t, TextureCache::ID>>& getTextures() const noexcept;

private:
	void destroy() noexcept;

	struct RawVertex {
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uv;
		TextureCache::ID textureID;

		bool operator==(const RawVertex& other) const noexcept {
			return position == other.position &&
				   normal == other.normal &&
				   uv == other.uv &&
				   textureID == other.textureID;
		}
	};

	static inline void hashCombine(std::size_t& seed, std::size_t value) noexcept {
		seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	}

	struct RawVertexHash {
		std::size_t operator()(const RawVertex& v) const noexcept {
			std::size_t seed = 0;

			hashCombine(seed, std::hash<float>{}(v.position.x));
			hashCombine(seed, std::hash<float>{}(v.position.y));
			hashCombine(seed, std::hash<float>{}(v.position.z));

			hashCombine(seed, std::hash<float>{}(v.normal.x));
			hashCombine(seed, std::hash<float>{}(v.normal.y));
			hashCombine(seed, std::hash<float>{}(v.normal.z));

			hashCombine(seed, std::hash<float>{}(v.uv.x));
			hashCombine(seed, std::hash<float>{}(v.uv.y));

			hashCombine(seed, std::hash<TextureCache::ID>{}(v.textureID));

			return seed;
		}

	};

	[[nodiscard]]
	std::vector<RawVertex> getRawVertices(const std::filesystem::path& file, vk::CommandPool commandPool) const;
	[[nodiscard]]
	std::pair<std::vector<uint32_t>, std::vector<Vertex>> getIndecies(const std::vector<RawVertex>& rawVertices);
	void calculateBoundingSphere(const std::vector<Vertex>& vertices) noexcept;
	void createBuffers(const std::vector<uint32_t>& indecies, const std::vector<Vertex>& vertices, vk::CommandPool commandPool);
	void copyBuffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size, vk::CommandPool commandPool);
	[[nodiscard]]
	std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> createGPUBuffer(
		Buffer& buffer, vk::BufferUsageFlags usage, vk::CommandPool commandPool);

private:
	uint32_t vertexCount;
	vk::raii::DeviceMemory vertexMemory;
	vk::raii::Buffer vertexBuffer;

	std::vector<std::pair<uint32_t, TextureCache::ID>> _textureIndecies = {};
	BoundingSphere _boundingSphere;

	uint32_t indexCount;
	vk::raii::DeviceMemory indexMemory;
	vk::raii::Buffer indexBuffer;
};

// TODO: batch game objects that use the same model to reduce draw calls, but for now this is good enough
// should probably drop the creation transform as it is just a shortcut
// also this shit aint RAII, instead of using id use handles that call unload

class ModelCache {
public:
	// 0 is reserved for no model
	using ID = size_t;

	[[nodiscard]]
	static Model3D& getModel(ID id) noexcept;

	[[nodiscard]]
	static ID loadModel(std::filesystem::path file, vk::CommandPool commandPool = nullptr);
	static void unloadModel(ID id) noexcept;

	[[nodiscard]]
	static size_t getSize() noexcept {
		return _idMap.size();
	}

private:
	static inline std::unordered_map<std::filesystem::path, ID> _idMap;
	static inline std::unordered_map<ID, size_t> _refCounts;
	static inline std::unordered_map<ID, Model3D> _cache;
	static inline std::mutex _mutex;
};
}
