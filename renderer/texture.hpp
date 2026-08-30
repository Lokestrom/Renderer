#pragma once

#include "Buffer.hpp"

#include <array>
#include <filesystem>
#include <future>
#include <memory>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

namespace renderer {

struct UniformBufferObject;

using BindlessTextureIndex = uint32_t;

inline constexpr BindlessTextureIndex InvalidBindlessTextureIndex = std::numeric_limits<BindlessTextureIndex>::max();
inline constexpr uint32_t MaxBindlessTextures = 1024;
inline constexpr uint32_t FullTextureResolution = 0;
inline constexpr uint32_t LowTextureStreamMaxDimension = 256;
inline constexpr uint32_t MediumTextureStreamMaxDimension = 1024;
inline constexpr size_t MaxTextureStreamUploadsPerFrame = 1;
inline constexpr size_t MaxTextureStreamPrepareJobs = 2;
inline constexpr uint64_t TextureStreamDemotionDelayFrames = 300;
inline constexpr uint64_t TextureStreamDemotionIntervalFrames = 30;
inline constexpr uint64_t RetiredTextureResourceFrameDelay = 4;

class TextureCache;

// creation must be thread safe
class Texture {
public:
	~Texture() noexcept;

	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;
	Texture(Texture&&) noexcept;
	Texture& operator=(Texture&&) = delete;

	void bind(vk::CommandBuffer cmd, vk::PipelineLayout layout) const noexcept;

private:
	friend class TextureCache;

	struct PreparedUpload {
		std::vector<unsigned char> pixels;
		uint32_t width = 1;
		uint32_t height = 1;
		uint32_t sourceWidth = 1;
		uint32_t sourceHeight = 1;
		uint32_t residentMaxDimension = 1;
		uint32_t mipLevels = 1;
	};

	struct UploadedImage {
		vk::raii::DeviceMemory imageMemory = nullptr;
		vk::raii::Image image = nullptr;
		vk::raii::ImageView imageView = nullptr;
		uint32_t mipLevels = 1;
		uint32_t sourceWidth = 1;
		uint32_t sourceHeight = 1;
		uint32_t residentMaxDimension = 1;
	};

	explicit Texture(
		const std::filesystem::path& file,
		const glm::vec3& color,
		BindlessTextureIndex bindlessIndex,
		vk::CommandPool commandPool);
	explicit Texture(const glm::vec3& color) noexcept;
	Texture();

	[[nodiscard]]
	vk::DescriptorImageInfo descriptorInfo() const noexcept;

	[[nodiscard]]
	static PreparedUpload prepareUpload(const std::filesystem::path& file, uint32_t maxResidentDimension);
	[[nodiscard]]
	static UploadedImage uploadPrepared(const PreparedUpload& preparedUpload, vk::CommandPool commandPool);

	void loadNoTexture(vk::CommandPool commandPool = nullptr);
	void loadFromFile(const std::filesystem::path& file, uint32_t maxResidentDimension, vk::CommandPool commandPool);
	void installUploadedImage(UploadedImage&& uploadedImage);
	void requestMaxResidentDimension(uint32_t maxResidentDimension, uint64_t requestFrame, float priority) noexcept;
	[[nodiscard]]
	uint32_t streamingTargetForFrame(uint64_t requestFrame) const noexcept;
	[[nodiscard]]
	bool hasActiveStreamingPrepare() const noexcept;
	[[nodiscard]]
	bool streamingPrepareReady() const;
	[[nodiscard]]
	bool startStreamingPrepare(uint64_t requestFrame);
	[[nodiscard]]
	bool finishStreamingPrepare();
	void createSampler();
	static void resetSampler() noexcept;
	void releaseBindlessSlot() noexcept;

private:
	glm::vec3 _color{1.0};
	std::filesystem::path _file;
	BindlessTextureIndex _bindlessIndex = InvalidBindlessTextureIndex;
	// TODO: This may be better in a external container to avoid having a lot of memory allocated unused images
	// Using 32*3 bytes even if it is not used, and it is only needed for init and destruction so no real preformance impact
	// can use only 8 bytes for a ptr instead
	vk::raii::DeviceMemory _imageMemory = nullptr;
	vk::raii::Image _image = nullptr;
	vk::raii::ImageView _imageView = nullptr;
	static inline vk::raii::Sampler _sampler = nullptr;
	uint32_t _mipLevels = 1;
	uint32_t _sourceWidth = 1;
	uint32_t _sourceHeight = 1;
	uint32_t _residentMaxDimension = 1;
	uint32_t _requestedMaxDimension = LowTextureStreamMaxDimension;
	uint32_t _lowerRequestedMaxDimension = LowTextureStreamMaxDimension;
	float _streamingPriority = std::numeric_limits<float>::max();
	uint64_t _lastStreamingRequestFrame = 0;
	uint64_t _lowerRequestSinceFrame = 0;
	uint64_t _imageVersion = 0;
	std::array<uint64_t, 2> _descriptorVersions{};
	std::future<UploadedImage> _streamingPrepare;
};

// Only loading is thread safe
class TextureCache {
	friend class Renderer;
	friend class Texture;

public:
	// 0 is reserved for default texture
	using ID = size_t;
	struct StreamingRequest {
		ID id;
		uint32_t maxResidentDimension;
		float priority;
	};

	[[nodiscard]]
	static Texture& getTexture(ID id) noexcept;

	[[nodiscard]]
	static ID loadTexture(const glm::vec3& color, const std::filesystem::path& file = "", vk::CommandPool commandPool = nullptr);
	static void unloadTexture(ID id) noexcept;
	static void beginStreamingFrame(uint32_t descriptorSetIndex) noexcept;
	static void requestTextureResolutions(std::span<const StreamingRequest> requests);
	static void applyStreamingRequests();

	static void loadDefault();
	static void unloadDefault() noexcept;

private:
	static void initializeBindlessDescriptorSets(
		const std::array<vk::DescriptorSet, 2>& descriptorSets) noexcept;
	[[nodiscard]]
	static BindlessTextureIndex registerDefaultTexture(const Texture& texture) noexcept;
	[[nodiscard]]
	static BindlessTextureIndex reserveTextureSlot();
	static void releaseTextureSlot(BindlessTextureIndex index) noexcept;
	static void writeBindlessTextureDescriptorToAllSets(
		BindlessTextureIndex index,
		const vk::DescriptorImageInfo& descriptorInfo) noexcept;
	static void writeBindlessTextureDescriptor(
		BindlessTextureIndex index,
		const vk::DescriptorImageInfo& descriptorInfo) noexcept;
	static void syncActiveFrameDescriptors() noexcept;
	static void collectRetiredTextureResources() noexcept;

	struct TextureHash {
	private:
		static constexpr void hashCombine(std::size_t& seed, std::size_t value) noexcept {
			seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		}

	public:
		std::size_t operator()(
			const std::pair<glm::vec3, std::filesystem::path>& key) const noexcept {
			const auto& [color, file] = key;

			std::size_t seed = 0;

			hashCombine(seed, std::hash<float>{}(color.x));
			hashCombine(seed, std::hash<float>{}(color.y));
			hashCombine(seed, std::hash<float>{}(color.z));
			hashCombine(seed, std::hash<std::filesystem::path>{}(file));

			return seed;
		}
	};

	static inline std::unordered_map<std::pair<glm::vec3, std::filesystem::path>, ID, TextureHash> _idMap;
	static inline std::unordered_map<ID, size_t> _refCounts;
	static inline std::unordered_map<ID, Texture> _cache;
	static inline std::mutex _mutex;
	static inline std::mutex _loadMutex;

	struct RetiredTextureImage {
		Texture::UploadedImage image;
		uint64_t retireAfterFrame = 0;
	};

	static inline std::array<vk::DescriptorSet, 2> _bindlessDescriptorSets{};
	static inline size_t _activeDescriptorSetIndex = 0;
	static inline std::array<bool, MaxBindlessTextures> _occupiedTextureSlots{};
	static inline BindlessTextureIndex _firstFreeTextureSlot = InvalidBindlessTextureIndex;
	// maybe enable nullDescriptor vk option this allows for not storing this
	static inline vk::DescriptorImageInfo _defaultTextureInfo{};
	static inline uint64_t _streamingFrame = 0;
	static inline std::vector<RetiredTextureImage> _retiredTextureImages;
	static inline std::vector<Texture*> _upgradeCandidates;
};

class TextureStreamer {
public:

	void update(const UniformBufferObject& ubo, uint32_t frameIndex, float dynamicObjectViewDistance);

private:
	[[nodiscard]]
	uint32_t textureResolutionForDistance(float distance) noexcept;
	[[nodiscard]]
	glm::ivec2 updateStaticChunkCenter(const glm::vec2& cameraPosition) noexcept;

private:
	struct ModelStreamingRequest {
		uint32_t maxResidentDimension;
		float priority;
	};

	glm::ivec2 _staticChunkCenter{ 0 };
	bool _hasStaticChunkCenter = false;

	// keeps the capacity of the vector to avoid reallocations and copies during streaming updates
	std::vector<TextureCache::StreamingRequest> _requests;
};

}
