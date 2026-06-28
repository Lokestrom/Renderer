#pragma once

#include "VulkanApp.hpp"
#include "Buffer.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <mutex>
#include <string>

#include <glm/glm.hpp>

namespace vulkan {

using BindlessTextureIndex = uint32_t;

inline constexpr BindlessTextureIndex InvalidBindlessTextureIndex = std::numeric_limits<BindlessTextureIndex>::max();
inline constexpr uint32_t MaxBindlessTextures = 1024;

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

	explicit Texture(
		const std::filesystem::path& file,
		const glm::vec3& color,
		BindlessTextureIndex bindlessIndex,
		vk::CommandPool commandPool);
	explicit Texture(const glm::vec3& color);
	Texture();

	[[nodiscard]]
	vk::DescriptorImageInfo descriptorInfo() const noexcept;

	void loadNoTexture(vk::CommandPool commandPool = nullptr);
	void loadFromFile(const std::filesystem::path& file, vk::CommandPool commandPool);
	void createImageView();
	void createSampler();
	void destroyImageResources() noexcept;
	static void destroySampler() noexcept;
	void releaseBindlessSlot() noexcept;

	void changeImageLayout(vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
		vk::PipelineStageFlagBits source, vk::PipelineStageFlagBits destination,
		vk::AccessFlagBits srcAccessMask, vk::AccessFlagBits dstAccessMask, vk::CommandPool commandPool);
	void copyBufferToImage(Buffer& buffer, unsigned int width, unsigned int height, vk::CommandPool commandPool);

private:
	glm::vec3 _color{1.0};
	BindlessTextureIndex _bindlessIndex = InvalidBindlessTextureIndex;
	vk::Image _image = nullptr;
	vk::ImageView _imageView = nullptr;
	static inline vk::Sampler _sampler = nullptr;
	vk::DeviceMemory _imageMemory = nullptr;
};

class TextureCache {
	friend class Renderer;
	friend class Texture;

public:
	// 0 is reserved for default texture
	using ID = size_t;

	[[nodiscard]]
	static Texture& getTexture(ID id);

	[[nodiscard]]
	static ID loadTexture(const glm::vec3& color, const std::filesystem::path& file = "", vk::CommandPool commandPool = nullptr);
	static void unloadTexture(ID id);

	static void loadDefault();
	static void unloadDefault() noexcept;

private:
	static void initializeBindlessDescriptorSet(vk::DescriptorSet descriptorSet) noexcept;
	static void clearBindlessDescriptorSet() noexcept;

	[[nodiscard]]
	static BindlessTextureIndex registerDefaultTexture(const Texture& texture) noexcept;
	[[nodiscard]]
	static BindlessTextureIndex reserveTextureSlot();
	static void unregisterTexture(BindlessTextureIndex index) noexcept;
	static void writeBindlessTextureDescriptor(
		BindlessTextureIndex index,
		const vk::DescriptorImageInfo& descriptorInfo) noexcept;

	struct TextureHash {
	private:
		static void hashCombine(std::size_t& seed, std::size_t value) noexcept {
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

	static inline vk::DescriptorSet _bindlessDescriptorSet;
	static inline std::array<bool, MaxBindlessTextures> _occupiedTextureSlots{};
	static inline BindlessTextureIndex _firstFreeTextureSlot = InvalidBindlessTextureIndex;
	// maybe enable nullDescriptor vk option this allows for not storing this
	static inline vk::DescriptorImageInfo _defaultTextureInfo{};
};

}
