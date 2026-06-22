#include "texture.hpp"

#include "helpers.hpp"
#include "Renderer.hpp"
#include "../console.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize2.h>

using namespace vulkan;

vulkan::Texture::Texture(const std::filesystem::path& file, const glm::vec3& color, const Renderer& renderer, vk::CommandPool commandPool) 
	: _descriptorPool(renderer._descriptorPool) 
	, _color(color) {
	assert(std::filesystem::is_regular_file(file) && "The path was not a file");

	loadFromFile(file, commandPool);
	createImageView();
	createDescriptorSet(renderer);
}

vulkan::Texture::Texture(const glm::vec3& color, const Renderer& renderer, vk::CommandPool commandPool) 
	: _descriptorPool(renderer._descriptorPool) 
	, _color(color) {
	loadNoTexture(commandPool);
	createImageView();
	createDescriptorSet(renderer);
}

vulkan::Texture::Texture(const Renderer& renderer) 
	: _descriptorPool(renderer._descriptorPool) {
	assert(!TextureCache::_cache.contains(0) && "Attempting to create multiple default textures");

	loadNoTexture();
	createImageView();
	createSampler();
	createDescriptorSet(renderer);
}

vulkan::Texture::~Texture() {
	App::device.destroyImageView(_imageView);
	App::device.destroyImage(_image);
	App::device.freeMemory(_imageMemory);
	if (_descriptorPool)
		App::device.freeDescriptorSets(_descriptorPool, _descriptorSet);
}

vulkan::Texture::Texture(Texture&& other) noexcept
	: _image(std::exchange(other._image, nullptr))
	, _imageMemory(std::exchange(other._imageMemory, nullptr))
	, _imageView(std::exchange(other._imageView, nullptr))
	, _descriptorSet(std::exchange(other._descriptorSet, nullptr))
	, _descriptorPool(std::exchange(other._descriptorPool, nullptr))
	, _color(other._color) 
{}

Texture& vulkan::Texture::operator=(Texture&& other) noexcept {
	if (this == &other)
		return *this;
	
	this->~Texture();
	_image = std::exchange(other._image, nullptr);
	_imageMemory = std::exchange(other._imageMemory, nullptr);
	_imageView = std::exchange(other._imageView, nullptr);
	_sampler = std::exchange(other._sampler, nullptr);
	_descriptorSet = std::exchange(other._descriptorSet, nullptr);
	_descriptorPool = std::exchange(other._descriptorPool, nullptr);
	_color = other._color;
	return *this;
}

void vulkan::Texture::bind(vk::CommandBuffer cmd, vk::PipelineLayout layout) const noexcept {
	FragmentPushConstant fragmentPush{ .color = { _color, 1. } };
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eFragment, sizeof(VertexPushConstant), sizeof(FragmentPushConstant), &fragmentPush);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 1, _descriptorSet, {});
}

void vulkan::Texture::loadNoTexture(vk::CommandPool commandPool) {
	std::array<unsigned char, 4> pixels;
	pixels.fill(255);
	Buffer stagingBuffer(pixels.size(), 1, vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	if(stagingBuffer.map() != vk::Result::eSuccess)
		throw std::runtime_error("Failed to map a buffer when creating a color texture");
	stagingBuffer.writeToBuffer(pixels.data());
	stagingBuffer.unmap();

	vk::ImageCreateInfo info{
		.imageType = vk::ImageType::e2D,
		.format = vk::Format::eR8G8B8A8Srgb,
		.extent = vk::Extent3D(1, 1, 1),
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = vk::SampleCountFlagBits::e1,
		.tiling = vk::ImageTiling::eOptimal,
		.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
		.sharingMode = vk::SharingMode::eExclusive,
		.initialLayout = vk::ImageLayout::eUndefined
	};

	if (App::device.createImage(&info, nullptr, &_image) != vk::Result::eSuccess) {
		throw std::runtime_error("Failed to create texture image");
	}

	auto memoryRequirements = App::device.getImageMemoryRequirements(_image);

	vk::MemoryAllocateInfo allocInfo{
		.allocationSize = memoryRequirements.size,
		.memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
	};

	if (App::device.allocateMemory(&allocInfo, nullptr, &_imageMemory) != vk::Result::eSuccess) {
		throw std::runtime_error("Failed to allocate memory for texture image");
	}

	App::device.bindImageMemory(_image, _imageMemory, 0);

	// TODO:
	// Maybe have a single commandBuffer to not create so many
	changeImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
		{}, vk::AccessFlagBits::eTransferWrite, commandPool);

	copyBufferToImage(stagingBuffer, 1, 1, commandPool);

	changeImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
		vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
		vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, commandPool);
}

void vulkan::Texture::loadFromFile(const std::filesystem::path& file, vk::CommandPool commandPool) {
	assert(std::filesystem::is_regular_file(file) && "Must be a file");
	int width, height, channels;
	stbi_uc* pixels = stbi_load(file.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
	if (!pixels) {
		throw std::runtime_error(
			"Failed to load image '" +
			file.string() +
			"': " +
			stbi_failure_reason());
	}

	if (width > 1024 || height > 1024) {
		float scale = std::min(
			1024. / (float)width,
			1024. / (float)height);
		// If the texture is really long this prevents 0
		int resizeWidth = std::max(1, (int)(width * scale));
		int resizeHeight =  std::max(1, (int)(height * scale));

		stbi_uc* resizePixels = stbir_resize_uint8_linear(
			pixels, width, height, 0,
			0, resizeWidth, resizeHeight, 0,
			STBIR_RGBA);
		if (!resizePixels) {
			stbi_image_free(pixels);
			throw std::runtime_error(
				"Failed to resize image '" +
				file.string() +
				"': " +
				stbi_failure_reason());
		}

		width = resizeWidth;
		height = resizeHeight;

		stbi_image_free(pixels);
		pixels = resizePixels;
	}

	Buffer stagingBuffer;
	try {
		stagingBuffer = std::move(Buffer(width * height * 4, 1, vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
	}
	catch (std::exception& e){
		stbi_image_free(pixels);
		throw std::runtime_error(
			std::string("Failed to create texture buffer, with") + e.what()
		);
	}
	if(stagingBuffer.map() != vk::Result::eSuccess){
		stbi_image_free(pixels);
		throw std::runtime_error("Failed to map buffer when loading texture");
	}
	stagingBuffer.writeToBuffer(pixels);
	stagingBuffer.unmap();
	stbi_image_free(pixels);

	vk::ImageCreateInfo info{
		.imageType = vk::ImageType::e2D,
		.format = vk::Format::eR8G8B8A8Srgb,
		.extent = vk::Extent3D(width, height, 1),
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = vk::SampleCountFlagBits::e1,
		.tiling = vk::ImageTiling::eOptimal,
		.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
		.sharingMode = vk::SharingMode::eExclusive,
		.initialLayout = vk::ImageLayout::eUndefined
	};

	if (App::device.createImage(&info, nullptr, &_image) != vk::Result::eSuccess) {
		throw std::runtime_error("Failed to create texture image");
	}

	auto memoryRequirements = App::device.getImageMemoryRequirements(_image);

	vk::MemoryAllocateInfo allocInfo {
		.allocationSize = memoryRequirements.size,
		.memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
	};

	if (App::device.allocateMemory(&allocInfo, nullptr, &_imageMemory) != vk::Result::eSuccess) {
		throw std::runtime_error("Failed to allocate memory for texture image");
	}

	App::device.bindImageMemory(_image, _imageMemory, 0);

	// TODO:
	// Maybe have a single commandBuffer to not create so many
	changeImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
		{}, vk::AccessFlagBits::eTransferWrite, commandPool);

	copyBufferToImage(stagingBuffer, width, height, commandPool);

	try {
	changeImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
		vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
		vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, commandPool);
	}
	catch (std::exception& e) {
		throw std::runtime_error(std::string("Failed to change image layout to shader read: ") + e.what());
	}
}

void vulkan::Texture::createImageView() {
	vk::ImageViewCreateInfo viewInfo{};
	viewInfo.image = _image;
	viewInfo.viewType = vk::ImageViewType::e2D;
	viewInfo.format = vk::Format::eR8G8B8A8Srgb;
	viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	vk::ImageView imageView;
	if (App::device.createImageView(&viewInfo, nullptr, &_imageView) != vk::Result::eSuccess) {
		throw std::runtime_error("Failed to create image view for texture");
	}
}

void vulkan::Texture::createSampler() {
	vk::SamplerCreateInfo samplerInfo{};
	samplerInfo.magFilter = vk::Filter::eLinear;
	samplerInfo.minFilter = vk::Filter::eLinear;
	samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
	samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
	samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
	// TODO: enable this
	samplerInfo.anisotropyEnable = VK_FALSE;

	VkPhysicalDeviceProperties properties = App::physicalDevice.getProperties();
	samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
	samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;

	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = vk::CompareOp::eAlways;
	samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 1.0f;

	if (App::device.createSampler(&samplerInfo, nullptr, &_sampler) != vk::Result::eSuccess) {
		throw std::runtime_error("Failed to create texture sampler");
	}
}

std::mutex descriptorSetMutex;

void vulkan::Texture::createDescriptorSet(const Renderer& renderer) {
	// TODO: this is really bad, in the future this will be removed in place of bindless descriptor sets
	std::lock_guard<std::mutex> lock(descriptorSetMutex);
	vk::DescriptorSetAllocateInfo allocInfo{};
	allocInfo.descriptorPool = renderer._descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &renderer._textureDescriptorSetLayout;

	_descriptorSet = App::device.allocateDescriptorSets(allocInfo)[0];
	
	vk::DescriptorImageInfo textureInfo{
		.sampler = _sampler,
		.imageView = _imageView,
		.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
	};

	vk::WriteDescriptorSet descriptorWrite{};
	descriptorWrite.dstSet = _descriptorSet;
	descriptorWrite.dstBinding = 0;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo = &textureInfo;

	App::device.updateDescriptorSets(descriptorWrite, {});
}

void vulkan::Texture::changeImageLayout(vk::ImageLayout oldLayout, vk::ImageLayout newLayout, 
	vk::PipelineStageFlagBits source, vk::PipelineStageFlagBits destination, 
	vk::AccessFlagBits srcAccessMask, vk::AccessFlagBits dstAccessMask,
	vk::CommandPool commandPool) {
	vk::CommandBuffer commandBuffer = beginSingleTimeCommands(commandPool);

	vk::ImageMemoryBarrier barrier{};
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = _image;
	barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	vk::PipelineStageFlags sourceStage;
	vk::PipelineStageFlags destinationStage;

	barrier.srcAccessMask = srcAccessMask;
	barrier.dstAccessMask = dstAccessMask;

	sourceStage = source;
	destinationStage = destination;

	commandBuffer.pipelineBarrier(sourceStage, destinationStage, {}, {}, {}, barrier);

	endSingleTimeCommands(commandBuffer, commandPool);
}

void vulkan::Texture::copyBufferToImage(Buffer& buffer, unsigned int width, unsigned int height, vk::CommandPool commandPool) {
	vk::CommandBuffer commandBuffer = beginSingleTimeCommands(commandPool);

	vk::BufferImageCopy region{};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = {
		static_cast<unsigned int>(width),
		static_cast<unsigned int>(height),
		1
	};

	commandBuffer.copyBufferToImage(buffer.getBuffer(), _image, vk::ImageLayout::eTransferDstOptimal, region);

	endSingleTimeCommands(commandBuffer, commandPool);
}

Texture& TextureCache::getTexture(ID id) {
	assert(_cache.contains(id) && "TextureCache does not contain model with given id");
	return _cache.at(id);
}

#include <unordered_set>

TextureCache::ID TextureCache::loadTexture(const glm::vec3& color, const std::filesystem::path& file, vk::CommandPool commandPool) {
	assert(std::filesystem::is_regular_file(file) || file.empty() && "The texture must be a file or empty");
	static std::atomic<ID> currID = 1;
	static std::unordered_set<std::pair<glm::vec3, std::filesystem::path>, TextureHash> failedPaths;

	auto key = std::make_pair(color, file);

	ID id;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (failedPaths.contains(key)) {
			Console::log(Console::Log::Type::message, "Tried loading already failed file: " + file.string());
			return 0;
		}
		if (_idMap.contains(key)) {
			id = _idMap.at(key);
			if (id == 0) {
				return 0;
			}
			_refCounts[id]++;
			return id;
		}
		id = currID.fetch_add(1);
		_idMap[key] = id;
		_refCounts[id] = 1;
	}

	std::unique_ptr<Texture> texture;
	try {
		// this is the important part to not be in a lock
		if (file.empty()) {
			texture = std::make_unique<Texture>(color, *App::renderer, commandPool);
		}
		else {
			texture = std::make_unique<Texture>(file, color, *App::renderer, commandPool);
		}
	}
	catch (std::exception& e) {
		Console::log(Console::Log::Type::error, std::string("Failed to create texture: ") + file.string() + "\nWith:" + e.what());
		std::lock_guard<std::mutex> lock(_mutex);
		failedPaths.emplace(key);
		_idMap.erase(key);
		_refCounts.erase(id);
		throw std::runtime_error(std::string("Failed to create texture: ") + file.string() + "\nWith:" + e.what());
	}

	std::lock_guard<std::mutex> lock(_mutex);
	_cache.emplace(id, std::move(*texture.release()));
	return id;
}

void TextureCache::unloadTexture(ID id) {
	if (id == 0) {
		return;
	}
	assert(_refCounts[id] > 0 && "Texture reference count can't be 0 when unloading texture");
	_refCounts[id]--;
	if (_refCounts[id] != 0) {
		return;
	}
	_cache.erase(id);
	_refCounts.erase(id);
	for (auto it = _idMap.begin(); it != _idMap.end(); ++it) {
		if (it->second == id) {
			_idMap.erase(it);
			break;
		}
	}
}

void vulkan::TextureCache::loadDefault(const Renderer& renderer) {
	assert(!_cache.contains(0) && "Attempting to create multiple default textures");
	_idMap[std::make_pair(glm::vec3{ 1.0 }, "")] = 0;
	_cache.emplace(0, Texture(renderer));
	_refCounts[0] = 1;
}

void vulkan::TextureCache::unloadDefault() {
	assert(_cache.contains(0) && "Attempting to unload default before creating it");
	assert(_cache.size() == 1 && "There are still other textures loaded while unloading default");
	assert(_refCounts[0] == 1 && "Default texture reference count should be 1 while unloading it");
	_idMap.erase(std::make_pair(glm::vec3{ 1.0 }, ""));
	_cache.erase(0);
	_refCounts.erase(0);
	App::device.destroySampler(Texture::_sampler);
}