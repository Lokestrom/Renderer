#pragma once

#include "VulkanApp.hpp"
#include "Buffer.hpp"

#include <filesystem>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include <mutex>

namespace vulkan {

class TextureCache;

// creation must be thread safe
class Texture {
public:
	friend TextureCache;

	explicit Texture(const std::filesystem::path& file, const Renderer& renderer);
	~Texture();

	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;
	Texture(Texture&&) noexcept;
	Texture& operator=(Texture&&) noexcept;

	void bind(vk::CommandBuffer cmd, vk::PipelineLayout layout) const noexcept;

private:
	void loadFromFile(const std::filesystem::path& file);
	void createImageView();
	void createSampler();
	void createDescriptorSet(const Renderer& renderer);

	void changeImageLayout(vk::ImageLayout oldLayout, vk::ImageLayout newLayout, 
		vk::PipelineStageFlagBits source, vk::PipelineStageFlagBits destination,
		vk::AccessFlagBits srcAccessMask, vk::AccessFlagBits dstAccessMask);
	void copyBufferToImage(Buffer& buffer, unsigned int width, unsigned int height);

	//creates the default texture
	Texture(const Renderer& renderer);

private:
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
	static ID loadTexture(std::filesystem::path file);
	static void unloadTexture(ID id);

	static void loadDefault(const Renderer& renderer);
	static void unloadDefault();

private:
	static inline std::unordered_map<std::filesystem::path, ID> _idMap;
	static inline std::unordered_map<ID, size_t> _refCounts;
	static inline std::unordered_map<ID, Texture> _cache;
	static inline std::mutex _mutex;
};

}