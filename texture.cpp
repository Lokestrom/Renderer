#include "texture.hpp"

#include "helpers.hpp"
#include "Renderer.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

using namespace vulkan;

vulkan::Texture::Texture(const std::filesystem::path& file, const Renderer& renderer) 
	: _descriptorPool(renderer._descriptorPool) {
	assert(std::filesystem::is_regular_file(file) && "The path was not a file");

	loadFromFile(file);
	createImageView();
	createSampler();
	createDescriptorSet(renderer);
}

vulkan::Texture::Texture(const Renderer& renderer) 
	: _descriptorPool(renderer._descriptorPool) {
	assert(!TextureCache::_cache.contains(0) && "Attempting to create multiple default textures");

	std::array<unsigned char, 4> pixels;
	pixels.fill(255);
	Buffer stagingBuffer(pixels.size(), 1, vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	stagingBuffer.map();
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
		{}, vk::AccessFlagBits::eTransferWrite);

	copyBufferToImage(stagingBuffer, 1, 1);

	changeImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
		vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
		vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead);

	createImageView();
	createSampler();
	createDescriptorSet(renderer);
}

vulkan::Texture::~Texture() {
	App::device.destroySampler(_sampler);
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
	, _sampler(std::exchange(other._sampler, nullptr))
	, _descriptorSet(std::exchange(other._descriptorSet, nullptr)) 
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
	return *this;
}

void vulkan::Texture::bind(vk::CommandBuffer cmd, vk::PipelineLayout layout) const noexcept {
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 1, _descriptorSet, {});
}

void vulkan::Texture::loadFromFile(const std::filesystem::path& file) {
	int width, height, channels;
	stbi_uc* pixels = stbi_load(file.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
	assert(pixels && "Failed to load image");

	Buffer stagingBuffer( width * height * 4, 1, vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent );
	stagingBuffer.map();
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
		{}, vk::AccessFlagBits::eTransferWrite);

	copyBufferToImage(stagingBuffer, width, height);

	changeImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
		vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
		vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead);
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

void vulkan::Texture::createDescriptorSet(const Renderer& renderer) {
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
	vk::AccessFlagBits srcAccessMask, vk::AccessFlagBits dstAccessMask) {
	vk::CommandBuffer commandBuffer = beginSingleTimeCommands();

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

	endSingleTimeCommands(commandBuffer);
}

void vulkan::Texture::copyBufferToImage(Buffer& buffer, unsigned int width, unsigned int height) {
	vk::CommandBuffer commandBuffer = beginSingleTimeCommands();

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

	endSingleTimeCommands(commandBuffer);
}

Texture& TextureCache::getTexture(ID id) {
	assert(_cache.contains(id) && "TextureCache does not contain model with given id");
	return _cache.at(id);
}

TextureCache::ID TextureCache::loadTexture(std::filesystem::path file) {
	assert(std::filesystem::is_regular_file(file) && "The texture must be a file");
	static std::atomic<ID> currID = 1;

	ID id;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_idMap.contains(file)) {
			id = _idMap.at(file);
			_refCounts[id]++;
			return id;
		}
		id = currID.fetch_add(1);
		_idMap[file] = id;
		_refCounts[id] = 1;
	}

	//this is the important part to not be in a lock
	Texture texture{ file, *App::renderer };

	std::lock_guard<std::mutex> lock(_mutex);
	_cache.emplace(id, Texture(file, *App::renderer));
	return id;
}

void TextureCache::unloadTexture(ID id) {
	if (id == 0) {
		return;
	}
	_refCounts[id]--;
	assert(_refCounts[id] >= 0 && "Texture reference count can't be negative");
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
	_idMap[""] = 0;
	_cache.emplace(0, Texture(renderer));
	_refCounts[0] = 1;
}

void vulkan::TextureCache::unloadDefault() {
	assert(_cache.contains(0) && "Attempting to unload default before creating it");
	_idMap.erase("");
	_cache.erase(0);
	_refCounts.erase(0);
}