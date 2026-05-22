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
	struct Vertex {
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uv;

		static std::array<vk::VertexInputBindingDescription, 1> bindingDescriptions() noexcept;
		static std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions() noexcept;
	};

	Model3D()
		: vertexCount(0), vertexBuffer(nullptr), vertexMemory(nullptr), indexCount(0), indexBuffer(nullptr), indexMemory(nullptr) {}
	Model3D(std::filesystem::path file);

	Model3D(Model3D&) = delete;
	Model3D& operator=(Model3D&) = delete;

	Model3D(Model3D&&) noexcept;
	Model3D& operator=(Model3D&&) noexcept;

	~Model3D();

	void draw(vk::CommandBuffer cmd, vk::PipelineLayout layout) const noexcept;

private:
	void destroy();

	struct RawVertex {
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uv;
		TextureCache::ID textureID;

		bool operator==(const RawVertex& other) const {
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

	std::vector<RawVertex> getRawVertices(const std::filesystem::path& file) const;
	std::pair<std::vector<uint32_t>, std::vector<Vertex>> getIndecies(const std::vector<RawVertex>& rawVertices);
	void createBuffers(const std::vector<uint32_t>& indecies, const std::vector<Vertex>& vertices);
	void copyBuffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size);
	std::pair<vk::Buffer, vk::DeviceMemory> createGPUBuffer(Buffer& buffer, vk::BufferUsageFlags usage);

private:
	uint32_t vertexCount;
	vk::Buffer vertexBuffer;
	vk::DeviceMemory vertexMemory;

	std::vector<std::pair<uint32_t, TextureCache::ID>> _textureIndecies = {};

	uint32_t indexCount;
	vk::DeviceMemory indexMemory;
	vk::Buffer indexBuffer;
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
	static ID loadModel(std::filesystem::path file);
	static void unloadModel(ID id);

private:
	static inline std::unordered_map<std::filesystem::path, ID> _idMap;
	static inline std::unordered_map<ID, size_t> _refCounts;
	static inline std::unordered_map<ID, Model3D> _cache;
	static inline std::mutex _mutex;
};
}