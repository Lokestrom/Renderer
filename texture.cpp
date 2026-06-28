#include "texture.hpp"

#include "helpers.hpp"
#include "Renderer.hpp"
#include "../console.hpp"

#include <algorithm>
#include <atomic>
#include <memory>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize2.h>

using namespace vulkan;

vulkan::Texture::Texture(
	const std::filesystem::path& file,
	const glm::vec3& color,
	BindlessTextureIndex bindlessIndex,
	vk::CommandPool commandPool)
	: _color(color)
	, _bindlessIndex(bindlessIndex) {
	assert(_sampler && "Default texture sampler must be created before loading file textures");
	assert(_bindlessIndex > 0 && _bindlessIndex < MaxBindlessTextures && "Texture requires a reserved bindless slot");

	try {
		loadFromFile(file, commandPool);
		createImageView();
	}
	catch (...) {
		releaseBindlessSlot();
		destroyImageResources();
		throw;
	}
}

vulkan::Texture::Texture(const glm::vec3& color)
	: _color(color)
	, _bindlessIndex(0) {
	assert(_sampler && "Default texture sampler must be created before loading color textures");
}

vulkan::Texture::Texture() {
	assert(!TextureCache::_cache.contains(0) && "Attempting to create multiple default textures");
	assert(!_sampler && "Default texture sampler already exists");

	try {
		loadNoTexture();
		createImageView();
		createSampler();
		_bindlessIndex = TextureCache::registerDefaultTexture(*this);
	}
	catch (...) {
		destroyImageResources();
		destroySampler();
		throw;
	}
}

vulkan::Texture::~Texture() noexcept {
	if (_bindlessIndex == InvalidBindlessTextureIndex) {
		assert((!_imageView && !_image && !_imageMemory) && "Must not have a image if it is color or invalid");
		return;
	}
	if (_bindlessIndex != 0)
		releaseBindlessSlot();
	destroyImageResources();
}

void vulkan::Texture::destroyImageResources() noexcept {
	if (_imageView) {
		App::device.destroyImageView(_imageView);
	}
	if (_image) {
		App::device.destroyImage(_image);
	}
	if (_imageMemory) {
		App::device.freeMemory(_imageMemory);
	}
	_imageView = nullptr;
	_image = nullptr;
	_imageMemory = nullptr;
}

void vulkan::Texture::destroySampler() noexcept {
	if (_sampler) {
		App::device.destroySampler(_sampler);
	}
	_sampler = nullptr;
}

vulkan::Texture::Texture(Texture&& other) noexcept
	: _image(std::exchange(other._image, nullptr))
	, _imageMemory(std::exchange(other._imageMemory, nullptr))
	, _imageView(std::exchange(other._imageView, nullptr))
	, _bindlessIndex(std::exchange(other._bindlessIndex, InvalidBindlessTextureIndex))
	, _color(other._color) 
{}

void vulkan::Texture::bind(vk::CommandBuffer cmd, vk::PipelineLayout layout) const noexcept {
	assert(cmd && "Command buffer must be valid to bind texture constants");
	assert(layout && "Pipeline layout must be valid to bind texture constants");
	assert(_bindlessIndex != InvalidBindlessTextureIndex && "Texture must have a valid bindless descriptor slot before drawing");
	assert(_bindlessIndex < MaxBindlessTextures && "Texture bindless descriptor slot is out of range");

	FragmentPushConstant fragmentPush{
		.color = { _color, 1. },
		.textureIndex = _bindlessIndex
	};
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eFragment, sizeof(VertexPushConstant), sizeof(FragmentPushConstant), &fragmentPush);
}

vk::DescriptorImageInfo vulkan::Texture::descriptorInfo() const noexcept {
	assert(_sampler && "Texture descriptor requires a sampler");
	assert(_imageView && "Texture descriptor requires an image view");
	return vk::DescriptorImageInfo{
		.sampler = _sampler,
		.imageView = _imageView,
		.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
	};
}

void vulkan::Texture::releaseBindlessSlot() noexcept {
	TextureCache::unregisterTexture(_bindlessIndex);
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
	std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
		stbi_load(file.string().c_str(), &width, &height, &channels, STBI_rgb_alpha),
		stbi_image_free);
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
			pixels.get(), width, height, 0,
			0, resizeWidth, resizeHeight, 0,
			STBIR_RGBA);
		if (!resizePixels) {
			throw std::runtime_error(
				"Failed to resize image '" +
				file.string() +
				"': " +
				stbi_failure_reason());
		}

		width = resizeWidth;
		height = resizeHeight;

		pixels.reset(resizePixels);
	}

	Buffer stagingBuffer(width * height * 4, 1, vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	if(stagingBuffer.map() != vk::Result::eSuccess){
		throw std::runtime_error("Failed to map buffer when loading texture");
	}
	stagingBuffer.writeToBuffer(pixels.get());
	stagingBuffer.unmap();
	pixels.reset();

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

void TextureCache::initializeBindlessDescriptorSet(vk::DescriptorSet descriptorSet) noexcept {
	assert(descriptorSet && "Bindless descriptor set must be valid");
	assert(!_bindlessDescriptorSet && "Bindless descriptor set is already initialized");
	assert(_firstFreeTextureSlot == InvalidBindlessTextureIndex && "Bindless free list must be empty before initialization");

	_bindlessDescriptorSet = descriptorSet;
	_occupiedTextureSlots.fill(false);
	_firstFreeTextureSlot = 1;
}

// Large stack alloc but OK since it is only called once during startup
BindlessTextureIndex TextureCache::registerDefaultTexture(const Texture& texture) noexcept {
	assert(_bindlessDescriptorSet && "Bindless descriptor set must be initialized before registering textures");
	assert(!_occupiedTextureSlots[0] && "Default texture slot is already registered");

	_defaultTextureInfo = texture.descriptorInfo();
	_occupiedTextureSlots[0] = true;

	std::array<vk::DescriptorImageInfo, MaxBindlessTextures> defaultDescriptors;
	defaultDescriptors.fill(_defaultTextureInfo);

	vk::WriteDescriptorSet descriptorWrite{
		.dstSet = _bindlessDescriptorSet,
		.dstBinding = 0,
		.dstArrayElement = 0,
		.descriptorCount = MaxBindlessTextures,
		.descriptorType = vk::DescriptorType::eCombinedImageSampler,
		.pImageInfo = defaultDescriptors.data()
	};

	App::device.updateDescriptorSets(descriptorWrite, {});
	return 0;
}

BindlessTextureIndex TextureCache::reserveTextureSlot() {
	assert(_bindlessDescriptorSet && "Bindless descriptor set must be initialized before registering textures");

	if (_firstFreeTextureSlot == MaxBindlessTextures) {
		throw std::runtime_error("Bindless texture descriptor array is full");
	}
	BindlessTextureIndex index = _firstFreeTextureSlot;

	assert(index > 0 && index < MaxBindlessTextures && "First free texture slot must be a non-default slot in range");
	assert(!_occupiedTextureSlots[index] && "First free texture slot must be unused");
	_occupiedTextureSlots[index] = true;

	_firstFreeTextureSlot += 1;
	while (_firstFreeTextureSlot < MaxBindlessTextures 
		&& _occupiedTextureSlots[_firstFreeTextureSlot]) {
		_firstFreeTextureSlot++;
	}
	return index;
}

void TextureCache::unregisterTexture(BindlessTextureIndex index) noexcept {
	if (index == InvalidBindlessTextureIndex) {
		return;
	}

	assert(index != 0 && "The default texture must not be unregistered here");
	assert(index < MaxBindlessTextures && "Texture slot is outside the bindless descriptor array");
	assert(_occupiedTextureSlots[index] && "Attempted to unregister a texture slot that is not in use");

	writeBindlessTextureDescriptor(index, _defaultTextureInfo);
	_occupiedTextureSlots[index] = false;
	if (_firstFreeTextureSlot > index) {
		_firstFreeTextureSlot = index;
	}
}

void TextureCache::writeBindlessTextureDescriptor(
	BindlessTextureIndex index,
	const vk::DescriptorImageInfo& descriptorInfo) noexcept {
	assert(index < MaxBindlessTextures && "Texture slot is outside the bindless descriptor array");
	assert(_bindlessDescriptorSet && "Bindless descriptor set must be initialized before writing texture descriptors");
	assert(descriptorInfo.sampler && "The descriptor info must have a sampler");
	assert(descriptorInfo.imageView && "The descriptor info must have a image view");
	assert(descriptorInfo.imageLayout == vk::ImageLayout::eShaderReadOnlyOptimal && "The descriptor info image layout must be of correct type");

	vk::WriteDescriptorSet descriptorWrite {
		.dstSet = _bindlessDescriptorSet,
		.dstBinding = 0,
		.dstArrayElement = index,
		.descriptorCount = 1,
		.descriptorType = vk::DescriptorType::eCombinedImageSampler,
		.pImageInfo = &descriptorInfo
	};

	App::device.updateDescriptorSets(descriptorWrite, {});
}

Texture& TextureCache::getTexture(ID id) {
	assert(_cache.contains(id) && "TextureCache does not contain a texture with given id");
	return _cache.at(id);
}

// TODO: if one fails every copy using the failed one will point to a non existant texture and cause an
// exception in getTexture or hit the assertion as this is not how it should be
TextureCache::ID TextureCache::loadTexture(const glm::vec3& color, const std::filesystem::path& file, vk::CommandPool commandPool) {
	assert(std::filesystem::is_regular_file(file) || file.empty() && "The texture must be a file or empty");
	static std::atomic<ID> currID = 1;

	auto key = std::make_pair(color, file);

	ID id;
	BindlessTextureIndex bindlessIndex;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		const auto idIt = _idMap.find(key);
		if (idIt != _idMap.end()) {
			const ID existingID = idIt->second;
			if (existingID == 0) {
				return 0;
			}
			_refCounts[existingID]++;
			return existingID;
		}

		id = currID.fetch_add(1);
		bindlessIndex = file.empty() ? 0 : reserveTextureSlot();
		_idMap.emplace(key, id);
		_refCounts.emplace(id, 1);
	}

	Texture texture = [&]() {
		try {
			if (file.empty()) {
				return Texture(color);
			}
			return Texture(file, color, bindlessIndex, commandPool);
		}
		catch (const std::exception& e) {
			std::lock_guard<std::mutex> lock(_mutex);
			_idMap.erase(key);
			_refCounts.erase(id);
			Console::log(Console::Log::Type::error, std::string("Failed to create texture: ") + file.string() + "\nWith:" + e.what());
			throw std::runtime_error(std::string("Failed to create texture: ") + file.string() + "\nWith:" + e.what());
		}
	}();

	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (!file.empty()) {
			writeBindlessTextureDescriptor(bindlessIndex, texture.descriptorInfo());
		}
		assert(!_cache.contains(id) && "New texture ID must not already exist in the texture cache");
		_cache.emplace(id, std::move(texture));
	}
	return id;
}

void TextureCache::unloadTexture(ID id) {
	assert(_cache.contains(id) && "Texture is not loaded");
	assert(_refCounts.contains(id) && "Texture does not have a ref count");
	assert(_refCounts[id] > 0 && "Texture reference count can't be 0 when unloading texture");
	if (id == 0) {
		return;
	}
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
		assert(it != --_idMap.end() && "The id must be in the map");
	}
}

void vulkan::TextureCache::loadDefault() {
	assert(!_cache.contains(0) && "Attempting to create multiple default textures");
	assert(!_idMap.contains(std::make_pair(glm::vec3{ 1.0 }, "")) && "Default texture ID map entry must not already exist");
	assert(!_refCounts.contains(0) && "Default texture reference count must not already exist");

	try {
		_cache.emplace(0, Texture());
		_idMap.emplace(std::make_pair(glm::vec3{ 1.0 }, ""), 0);
		_refCounts.emplace(0, 1);
	}
	catch (std::exception& e) {
		// This is not recoverable
		Console::log(Console::Log::Type::error, std::string("Failed to create default texture, with: ") + e.what());
		throw std::runtime_error("Failed to create default texture");
	}
}

void vulkan::TextureCache::unloadDefault() noexcept {
	assert(_cache.size() == 1 && "There are still others or there are no textures loaded while unloading default");
	assert(_idMap.size() == 1 && "There are still others or there are no textures in _idMap");
	assert(_refCounts.size() == 1 && "There are still others or there are no textures in _refCounts");
	assert(_cache.contains(0) && "The remaining texture is not the default one");
	assert(_idMap.contains(std::make_pair(glm::vec3{ 1.0 }, "")) && "The remaining texture is not the default one");
	assert(_refCounts.contains(0) && "The remaining texture is not the default one");
	assert(_refCounts[0] == 1 && "Default texture reference count must be 1 while unloading it");

	_occupiedTextureSlots[0] = false;
	_defaultTextureInfo = {};

	_idMap.erase(std::make_pair(glm::vec3{ 1.0 }, ""));
	_cache.erase(0);
	_refCounts.erase(0);
	Texture::destroySampler();

	assert(std::none_of(
		   _occupiedTextureSlots.begin(),
		   _occupiedTextureSlots.end(),
		   [](bool occupied) { return occupied; }) &&
	   "All bindless texture slots must be released after the default texture is unloaded");
}
