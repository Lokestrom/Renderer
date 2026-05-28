#pragma once

#include "VulkanApp.hpp"
#include "Buffer.hpp"

#include <filesystem>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include <mutex>

#include <glm/glm.hpp>

namespace vulkan {

class TextureCache;

// creation must be thread safe
class Texture {
public:
	friend TextureCache;

	explicit Texture(const std::filesystem::path& file, const glm::vec3& color, const Renderer& renderer, vk::CommandPool commandPool = nullptr);
	explicit Texture(const glm::vec3& color, const Renderer& renderer, vk::CommandPool commandPool = nullptr);
	~Texture();

	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;
	Texture(Texture&&) noexcept;
	Texture& operator=(Texture&&) noexcept;

	void bind(vk::CommandBuffer cmd, vk::PipelineLayout layout) const noexcept;

private:
	void loadNoTexture(vk::CommandPool commandPool = nullptr);
	void loadFromFile(const std::filesystem::path& file, vk::CommandPool commandPool);
	void createImageView();
	void createSampler();
	void createDescriptorSet(const Renderer& renderer);

	void changeImageLayout(vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
		vk::PipelineStageFlagBits source, vk::PipelineStageFlagBits destination,
		vk::AccessFlagBits srcAccessMask, vk::AccessFlagBits dstAccessMask, vk::CommandPool commandPool);
	void copyBufferToImage(Buffer& buffer, unsigned int width, unsigned int height, vk::CommandPool commandPool);

	// creates the default texture
	Texture(const Renderer& renderer);

private:
	glm::vec3 _color{1.0};
	vk::DescriptorSet _descriptorSet;
	vk::DescriptorPool _descriptorPool;
	vk::Image _image = nullptr;
	vk::ImageView _imageView = nullptr;
	vk::Sampler _sampler = nullptr;
	vk::DeviceMemory _imageMemory = nullptr;
};

class TextureCache {
	friend Texture::Texture(const Renderer& renderer);

public:
	// 0 is reserved for default texture
	using ID = size_t;

	static Texture& getTexture(ID id);

	[[nodiscard]]
	static ID loadTexture(const glm::vec3& color, const std::filesystem::path& file = "", vk::CommandPool commandPool = nullptr);
	static void unloadTexture(ID id);

	static void loadDefault(const Renderer& renderer);
	static void unloadDefault();

private:
	struct TextureHash {
		size_t operator()(const std::pair<glm::vec3, std::filesystem::path>& key) const {
			const auto& [color, file] = key;

			std::string combined = std::to_string(color.x) + "," + std::to_string(color.y) + "," + std::to_string(color.z) 
				+ "," + file.string();
			return std::hash<std::string>{}(combined);
		}
	};

	static inline std::unordered_map<std::pair<glm::vec3, std::filesystem::path>, ID, TextureHash> _idMap;
	static inline std::unordered_map<ID, size_t> _refCounts;
	static inline std::unordered_map<ID, Texture> _cache;
	static inline std::mutex _mutex;
};

}