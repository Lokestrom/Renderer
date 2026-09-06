#pragma once

#include "Hash.hpp"
#include "texture.hpp"

#include <array>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan_raii.hpp>

namespace renderer {

class GameObject;

// creation must be thread safe
class Model3D {
	friend class ModelCache;

public:
	struct BoundingSphere {
		glm::vec3 center{ 0.0f };
		float radius = 0.0f;
	};

	struct Vertex {
		glm::vec3 position{};
		glm::vec3 normal{};
		glm::vec2 uv{};

		static std::array<vk::VertexInputBindingDescription, 1> bindingDescriptions() noexcept;
		static std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions() noexcept;

		bool operator==(const Vertex& other) const noexcept {
			return position == other.position &&
				   normal == other.normal &&
				   uv == other.uv;
		}
	};

	struct TextureIndex {
		uint32_t firstIndex = 0;
		TextureCache::ID textureID = 0;

		bool operator==(const TextureIndex&) const noexcept = default;
	};

	struct Data {
		std::vector<Vertex> vertices;
		std::vector<TextureIndex> textureIndices;

		Data() = default;
		~Data() noexcept;
		Data(const Data&) = delete;
		Data& operator=(const Data&) = delete;
		Data(Data&& other) noexcept;
		Data& operator=(Data&& other) noexcept;
	};

	[[nodiscard]]
	static Data loadFile(const std::filesystem::path& file, vk::CommandPool commandPool = nullptr);

	[[nodiscard]]
	static bool isSupportedFile(const std::filesystem::path& file);
	[[nodiscard]]
	static bool isSupportedFileFormat(std::string fileExtension);

	Model3D(Model3D&) = delete;
	Model3D& operator=(Model3D&) = delete;

	Model3D(Model3D&&) noexcept;
	Model3D& operator=(Model3D&&) noexcept;

	~Model3D() noexcept;

	void draw(vk::CommandBuffer cmd, vk::PipelineLayout layout) const noexcept;
	void drawGeometry(vk::CommandBuffer cmd) const noexcept;
	[[nodiscard]]
	const BoundingSphere& getBoundingSphere() const noexcept { return _boundingSphere; }
	[[nodiscard]]
	const std::vector<TextureIndex>& getTextures() const noexcept;

private:
	Model3D()
		: vertexCount(0), vertexMemory(nullptr), vertexBuffer(nullptr), indexCount(0), indexMemory(nullptr), indexBuffer(nullptr) {}
	Model3D(Data&& data, vk::CommandPool commandPool);

	void destroy() noexcept;

	static inline void hashCombine(std::size_t& seed, std::size_t value) noexcept {
		seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	}

	struct VertexHash {
		std::size_t operator()(const Vertex& v) const noexcept {
			std::size_t seed = 0;

			hashCombine(seed, std::hash<float>{}(v.position.x));
			hashCombine(seed, std::hash<float>{}(v.position.y));
			hashCombine(seed, std::hash<float>{}(v.position.z));

			hashCombine(seed, std::hash<float>{}(v.normal.x));
			hashCombine(seed, std::hash<float>{}(v.normal.y));
			hashCombine(seed, std::hash<float>{}(v.normal.z));

			hashCombine(seed, std::hash<float>{}(v.uv.x));
			hashCombine(seed, std::hash<float>{}(v.uv.y));

			return seed;
		}

	};

	[[nodiscard]]
	std::pair<std::vector<uint32_t>, std::vector<Vertex>> getIndices(const std::vector<Vertex>& rawVertices) const;
	void calculateBoundingSphere(const std::vector<Vertex>& vertices) noexcept;
	void createBuffers(const std::vector<uint32_t>& indices, const std::vector<Vertex>& vertices, vk::CommandPool commandPool);
	void copyBuffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size, vk::CommandPool commandPool);
	[[nodiscard]]
	std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> createGPUBuffer(
		Buffer& buffer, vk::BufferUsageFlags usage, vk::CommandPool commandPool);

private:
	uint32_t vertexCount;
	vk::raii::DeviceMemory vertexMemory;
	vk::raii::Buffer vertexBuffer;

	std::vector<TextureIndex> _textureIndices = {};
	BoundingSphere _boundingSphere;

	uint32_t indexCount;
	vk::raii::DeviceMemory indexMemory;
	vk::raii::Buffer indexBuffer;
};

// TODO: batch game objects that use the same model to reduce draw calls, but for now this is good enough
// should probably drop the creation transform as it is just a shortcut
// also this shit aint RAII, instead of using id use handles that call unload

// Only loading is thread safe
class ModelCache {
public:
	// 0 is reserved for no model
	using ID = size_t;

	[[nodiscard]]
	static Model3D& getModel(ID id) noexcept;

	[[nodiscard]]
	static ID loadModel(Model3D::Data&& data, vk::CommandPool commandPool = nullptr);
	static void addModelRefrence(ID id);
	static void unloadModel(ID id) noexcept;

	[[nodiscard]]
	static size_t getSize() noexcept;
private:
	static inline std::unordered_map<Hash, ID> _idMap;
	static inline std::unordered_map<ID, size_t> _refCounts;
	static inline std::unordered_map<ID, Model3D> _cache;
	static inline std::mutex _mutex;
};
}
