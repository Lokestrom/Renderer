#include "texture.hpp"

#include "helpers.hpp"
#include "Renderer.hpp"
#include "gameObject.hpp"
#include "Runtime.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize2.h>

using namespace renderer;

namespace {

constexpr vk::Format textureFormat = vk::Format::eR8G8B8A8Srgb;
constexpr vk::FormatFeatureFlags mipmapFormatFeatures =
	vk::FormatFeatureFlagBits::eBlitSrc |
	vk::FormatFeatureFlagBits::eBlitDst |
	vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
constexpr float StaticTextureChunkBorderBufferFraction = 0.2f;

[[nodiscard]]
bool textureFormatSupportsMipmaps() noexcept {
	static const vk::FormatProperties formatProperties = renderer::App::physicalDevice.getFormatProperties(textureFormat);
	return (formatProperties.optimalTilingFeatures & mipmapFormatFeatures) == mipmapFormatFeatures;
}

[[nodiscard]]
constexpr uint32_t normalizedResidentMaxDimension(
	uint32_t requestedMaxDimension,
	uint32_t sourceWidth,
	uint32_t sourceHeight) noexcept {
	assert(sourceWidth > 0 && sourceHeight > 0 && "Source texture dimensions must be positive");
	const uint32_t sourceMaxDimension = std::max(sourceWidth, sourceHeight);
	if (requestedMaxDimension == FullTextureResolution || requestedMaxDimension >= sourceMaxDimension) {
		return sourceMaxDimension;
	}
	return std::max(1u, requestedMaxDimension);
}

[[nodiscard]]
constexpr uint32_t maxRequestedTextureResolution(uint32_t a, uint32_t b) noexcept {
	if (a == FullTextureResolution || b == FullTextureResolution) {
		return FullTextureResolution;
	}
	return std::max(a, b);
}

[[nodiscard]]
constexpr uint32_t textureResolutionForStaticChunk(const GameObjectContainer::StaticChunk& chunk) noexcept {
	const int chunkRing = std::max(std::abs(chunk.offset.x), std::abs(chunk.offset.y));
	if (chunkRing == 0) {
		return FullTextureResolution;
	}
	return MediumTextureStreamMaxDimension;
}

void writeTextureDescriptor(
	vk::DescriptorSet descriptorSet,
	BindlessTextureIndex index,
	const vk::DescriptorImageInfo& descriptorInfo) noexcept {
	assert(descriptorSet && "Bindless descriptor set must be initialized before writing texture descriptors");
	assert(index < MaxBindlessTextures && "Texture slot is outside the bindless descriptor array");
	assert(descriptorInfo.sampler && descriptorInfo.imageView &&
		   descriptorInfo.imageLayout == vk::ImageLayout::eShaderReadOnlyOptimal &&
		   "Texture descriptor info must be valid");

	const vk::WriteDescriptorSet descriptorWrite{
		.dstSet = descriptorSet,
		.dstBinding = 0,
		.dstArrayElement = index,
		.descriptorCount = 1,
		.descriptorType = vk::DescriptorType::eCombinedImageSampler,
		.pImageInfo = &descriptorInfo
	};
	renderer::App::device.updateDescriptorSets(descriptorWrite, {});
}

[[nodiscard]]
vk::raii::ImageView createImageView(vk::Image image, uint32_t mipLevels) {
	assert(image && "Image view creation requires a valid image");
	assert(mipLevels > 0 && "Image view creation requires at least one mip level");
	const vk::ImageViewCreateInfo viewInfo{
		.image = image,
		.viewType = vk::ImageViewType::e2D,
		.format = textureFormat,
		.subresourceRange = {
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0,
			.levelCount = mipLevels,
			.baseArrayLayer = 0,
			.layerCount = 1 }
	};

	return renderer::App::device.createImageView(viewInfo);
}

void transitionImageLayout(
	const vk::raii::CommandBuffer& commandBuffer,
	vk::Image image,
	uint32_t mipLevels,
	vk::ImageLayout oldLayout,
	vk::ImageLayout newLayout,
	vk::PipelineStageFlagBits source,
	vk::PipelineStageFlagBits destination,
	vk::AccessFlagBits srcAccessMask,
	vk::AccessFlagBits dstAccessMask) noexcept {
	assert(*commandBuffer && "Image transition requires a valid command buffer");
	assert(image && "Image transition requires a valid image");
	assert(mipLevels > 0 && "Image transition requires at least one mip level");
	const vk::ImageMemoryBarrier barrier{
		.srcAccessMask = srcAccessMask,
		.dstAccessMask = dstAccessMask,
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = {
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0,
			.levelCount = mipLevels,
			.baseArrayLayer = 0,
			.layerCount = 1 },
	};

	commandBuffer.pipelineBarrier(source, destination, {}, {}, {}, barrier);
}

void copyBufferToTextureImage(
	const vk::raii::CommandBuffer& commandBuffer,
	vk::Buffer buffer,
	vk::Image image,
	uint32_t width,
	uint32_t height) noexcept {
	assert(*commandBuffer && "Texture copy requires a valid command buffer");
	assert(buffer && image && "Texture copy requires valid buffer and image handles");
	assert(width > 0 && height > 0 && "Texture copy dimensions must be positive");

	const vk::BufferImageCopy region{
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource = {
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1 },
		.imageOffset = vk::Offset3D{ .x = 0, .y = 0, .z = 0 },
		.imageExtent = vk::Extent3D{ .width = width, .height = height, .depth = 1 }
	};

	commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);
}

void generateTextureMipmaps(
	const vk::raii::CommandBuffer& commandBuffer,
	vk::Image image,
	uint32_t mipLevels,
	uint32_t width,
	uint32_t height) noexcept {
	assert(*commandBuffer && "Mipmap generation requires a valid command buffer");
	assert(image && "Mipmap generation requires a valid image");
	assert(mipLevels > 1 && "Mipmap generation requires more than one mip level");
	assert(width > 0 && height > 0 && "Mipmap dimensions must be positive");

	int32_t mipWidth = static_cast<int32_t>(width);
	int32_t mipHeight = static_cast<int32_t>(height);

	for (uint32_t mipLevel = 1; mipLevel < mipLevels; ++mipLevel) {
		const vk::ImageMemoryBarrier transferSourceBarrier{
			.srcAccessMask = vk::AccessFlagBits::eTransferWrite,
			.dstAccessMask = vk::AccessFlagBits::eTransferRead,
			.oldLayout = vk::ImageLayout::eTransferDstOptimal,
			.newLayout = vk::ImageLayout::eTransferSrcOptimal,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = mipLevel - 1,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1 }
		};

		commandBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eTransfer,
			{},
			{},
			{},
			transferSourceBarrier);

		const int32_t nextMipWidth = std::max(mipWidth / 2, 1);
		const int32_t nextMipHeight = std::max(mipHeight / 2, 1);
		const vk::ImageBlit blit{
			.srcSubresource = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.mipLevel = mipLevel - 1,
				.baseArrayLayer = 0,
				.layerCount = 1 },
			.srcOffsets = vk::ArrayWrapper1D<vk::Offset3D, 2>{ std::array{ vk::Offset3D{ .x = 0, .y = 0, .z = 0 }, vk::Offset3D{ .x = mipWidth, .y = mipHeight, .z = 1 } } },
			.dstSubresource = { .aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = mipLevel, .baseArrayLayer = 0, .layerCount = 1 },
			.dstOffsets = vk::ArrayWrapper1D<vk::Offset3D, 2>{ std::array{ vk::Offset3D{ .x = 0, .y = 0, .z = 0 }, vk::Offset3D{ .x = nextMipWidth, .y = nextMipHeight, .z = 1 } } }
		};

		commandBuffer.blitImage(
			image,
			vk::ImageLayout::eTransferSrcOptimal,
			image,
			vk::ImageLayout::eTransferDstOptimal,
			blit,
			vk::Filter::eLinear);

		const vk::ImageMemoryBarrier shaderReadBarrier{
			.srcAccessMask = vk::AccessFlagBits::eTransferRead,
			.dstAccessMask = vk::AccessFlagBits::eShaderRead,
			.oldLayout = vk::ImageLayout::eTransferSrcOptimal,
			.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = mipLevel - 1,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1 }
		};

		commandBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eFragmentShader,
			{},
			{},
			{},
			shaderReadBarrier);

		mipWidth = nextMipWidth;
		mipHeight = nextMipHeight;
	}

	const vk::ImageMemoryBarrier finalShaderReadBarrier{
		.srcAccessMask = vk::AccessFlagBits::eTransferWrite,
		.dstAccessMask = vk::AccessFlagBits::eShaderRead,
		.oldLayout = vk::ImageLayout::eTransferDstOptimal,
		.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = {
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = mipLevels - 1,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1 }
	};

	commandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eFragmentShader,
		{},
		{},
		{},
		finalShaderReadBarrier);
}

}

renderer::Texture::Texture(
	const std::filesystem::path& file,
	const glm::vec3& color,
	BindlessTextureIndex bindlessIndex,
	vk::CommandPool commandPool)
	: _color(color)
	, _file(file)
	, _bindlessIndex(bindlessIndex) {
	assert(*_sampler && "Default texture sampler must be created before loading file textures");
	assert(_bindlessIndex > 0 && _bindlessIndex < MaxBindlessTextures && "Texture requires a reserved bindless slot");

	loadFromFile(file, LowTextureStreamMaxDimension, commandPool);
}

renderer::Texture::Texture(const glm::vec3& color) noexcept
	: _color(color)
	, _bindlessIndex(0) {
	assert(*_sampler && "Default texture sampler must be created before loading color textures");
}

renderer::Texture::Texture() {
	assert(!TextureCache::_cache.contains(0) && "Attempting to create multiple default textures");
	assert(!*_sampler && "Default texture sampler already exists");

	try {
		loadNoTexture();
		_imageView = createImageView(*_image, _mipLevels);
		createSampler();
		_bindlessIndex = TextureCache::registerDefaultTexture(*this);
	}
	catch (...) {
		resetSampler();
		throw;
	}
}

renderer::Texture::~Texture() noexcept {
	if (_bindlessIndex == InvalidBindlessTextureIndex) {
		assert((!*_imageView && !*_image && !*_imageMemory) && "Must not have a image if it is invalid");
		return;
	}
	assert(_bindlessIndex < MaxBindlessTextures && "Texture bindless descriptor slot is out of range");
	if (_bindlessIndex != 0)
		releaseBindlessSlot();
}

void renderer::Texture::resetSampler() noexcept {
	_sampler.clear();
}

renderer::Texture::Texture(Texture&& other) noexcept
	: _color(other._color)
	, _file(std::move(other._file))
	, _bindlessIndex(std::exchange(other._bindlessIndex, InvalidBindlessTextureIndex))
	, _imageMemory(std::move(other._imageMemory))
	, _image(std::move(other._image))
	, _imageView(std::move(other._imageView))
	, _mipLevels(other._mipLevels)
	, _sourceWidth(other._sourceWidth)
	, _sourceHeight(other._sourceHeight)
	, _residentMaxDimension(other._residentMaxDimension)
	, _requestedMaxDimension(other._requestedMaxDimension)
	, _lowerRequestedMaxDimension(other._lowerRequestedMaxDimension)
	, _streamingPriority(other._streamingPriority)
	, _lastStreamingRequestFrame(other._lastStreamingRequestFrame)
	, _lowerRequestSinceFrame(other._lowerRequestSinceFrame)
	, _imageVersion(other._imageVersion)
	, _descriptorVersions(other._descriptorVersions)
	, _streamingPrepare(std::move(other._streamingPrepare)) {}

void renderer::Texture::bind(vk::CommandBuffer cmd, vk::PipelineLayout layout) const noexcept {
	assert(cmd && "Command buffer must be valid to bind texture constants");
	assert(layout && "Pipeline layout must be valid to bind texture constants");
	assert(_bindlessIndex < MaxBindlessTextures && "Texture bindless descriptor slot is out of range");

	FragmentPushConstant fragmentPush{
		.color = { _color, 1. },
		.textureIndex = _bindlessIndex
	};
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eFragment, sizeof(VertexPushConstant), sizeof(FragmentPushConstant), &fragmentPush);
}

vk::DescriptorImageInfo renderer::Texture::descriptorInfo() const noexcept {
	assert(*_sampler && "Texture descriptor requires a sampler");
	assert(*_imageView && "Texture descriptor requires an image view");
	return vk::DescriptorImageInfo{
		.sampler = *_sampler,
		.imageView = *_imageView,
		.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
	};
}

void renderer::Texture::releaseBindlessSlot() noexcept {
	if (std::ranges::any_of(_descriptorVersions, [](uint64_t version) noexcept { return version != 0; })) {
		// Published texture destruction is only called after the renderer has waited for the GPU.
		TextureCache::writeBindlessTextureDescriptorToAllSets(_bindlessIndex, TextureCache::_defaultTextureInfo);
	}
	TextureCache::releaseTextureSlot(_bindlessIndex);
}

void renderer::Texture::loadNoTexture(vk::CommandPool commandPool) {
	constexpr std::array<unsigned char, 4> pixels{ 255, 255, 255, 255 };
	Buffer stagingBuffer(pixels.size(), 1, vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	stagingBuffer.map();
	stagingBuffer.writeToBuffer(pixels.data());
	stagingBuffer.unmap();

	const vk::ImageCreateInfo info{
		.imageType = vk::ImageType::e2D,
		.format = textureFormat,
		.extent = vk::Extent3D{ .width = 1, .height = 1, .depth = 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = vk::SampleCountFlagBits::e1,
		.tiling = vk::ImageTiling::eOptimal,
		.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
		.sharingMode = vk::SharingMode::eExclusive,
		.initialLayout = vk::ImageLayout::eUndefined
	};

	_image = renderer::App::device.createImage(info);

	const vk::MemoryRequirements memoryRequirements = _image.getMemoryRequirements();

	const vk::MemoryAllocateInfo allocInfo{
		.allocationSize = memoryRequirements.size,
		.memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
	};

	_imageMemory = renderer::App::device.allocateMemory(allocInfo);

	_image.bindMemory(*_imageMemory, 0);

	const vk::raii::CommandBuffer commandBuffer = beginSingleTimeCommands(commandPool);
	transitionImageLayout(commandBuffer, *_image, _mipLevels,
		vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
		{}, vk::AccessFlagBits::eTransferWrite);

	copyBufferToTextureImage(commandBuffer, stagingBuffer.getBuffer(), *_image, 1, 1);

	transitionImageLayout(commandBuffer, *_image, _mipLevels,
		vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
		vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
		vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead);
	endSingleTimeCommands(commandBuffer);
}

Texture::PreparedUpload renderer::Texture::prepareUpload(const std::filesystem::path& file, uint32_t maxResidentDimension) {
	assert(std::filesystem::is_regular_file(file) && "Must be a file");
	int width = 0;
	int height = 0;
	int channels = 0;
	std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
		stbi_load(file.string().c_str(), &width, &height, &channels, STBI_rgb_alpha),
		stbi_image_free);
	if (!pixels) {
		const char* const failureReason = stbi_failure_reason();
		throw std::runtime_error(
			"Failed to load image '" +
			file.string() +
			"': " +
			(failureReason != nullptr ? failureReason : "unknown stb_image error"));
	}
	if (width <= 0 || height <= 0) {
		throw std::runtime_error("Image '" + file.string() + "' has invalid dimensions");
	}

	const uint32_t sourceWidth = static_cast<uint32_t>(width);
	const uint32_t sourceHeight = static_cast<uint32_t>(height);
	const uint32_t residentMaxDimension = normalizedResidentMaxDimension(maxResidentDimension, sourceWidth, sourceHeight);

	if (static_cast<uint32_t>(std::max(width, height)) > residentMaxDimension) {
		const float scale = std::min(
			static_cast<float>(residentMaxDimension) / static_cast<float>(width),
			static_cast<float>(residentMaxDimension) / static_cast<float>(height));
		// If the texture is really long this prevents 0
		const int resizeWidth = std::max(1, static_cast<int>(static_cast<float>(width) * scale));
		const int resizeHeight = std::max(1, static_cast<int>(static_cast<float>(height) * scale));

		stbi_uc* const resizePixels = stbir_resize_uint8_srgb(
			pixels.get(), width, height, 0,
			0, resizeWidth, resizeHeight, 0,
			STBIR_RGBA);
		if (!resizePixels) {
			// resize dont write a error to stbi_failure_reason
			throw std::runtime_error(
				"Failed to resize image '" +
				file.string() + "'");
		}

		width = resizeWidth;
		height = resizeHeight;

		pixels.reset(resizePixels);
	}

	const uint32_t textureWidth = static_cast<uint32_t>(width);
	const uint32_t textureHeight = static_cast<uint32_t>(height);
	const uint32_t mipLevels = textureFormatSupportsMipmaps()
								   ? std::bit_width(std::max(textureWidth, textureHeight))
								   : 1;

	// TODO: here a copy of the buffer is made meybe cant not do that
	return PreparedUpload{
		.pixels = std::vector<unsigned char>(pixels.get(), pixels.get() + static_cast<size_t>(textureWidth) * textureHeight * 4),
		.width = textureWidth,
		.height = textureHeight,
		.sourceWidth = sourceWidth,
		.sourceHeight = sourceHeight,
		.residentMaxDimension = residentMaxDimension,
		.mipLevels = mipLevels
	};
}

Texture::UploadedImage renderer::Texture::uploadPrepared(const PreparedUpload& preparedUpload, vk::CommandPool commandPool) {
	Buffer stagingBuffer(static_cast<vk::DeviceSize>(preparedUpload.width) * preparedUpload.height * 4, 1, vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	stagingBuffer.map();
	stagingBuffer.writeToBuffer(preparedUpload.pixels.data());
	stagingBuffer.unmap();

	const vk::ImageCreateInfo info{
		.imageType = vk::ImageType::e2D,
		.format = textureFormat,
		.extent = vk::Extent3D{
			.width = preparedUpload.width,
			.height = preparedUpload.height,
			.depth = 1 },
		.mipLevels = preparedUpload.mipLevels,
		.arrayLayers = 1,
		.samples = vk::SampleCountFlagBits::e1,
		.tiling = vk::ImageTiling::eOptimal,
		.usage = (preparedUpload.mipLevels > 1 ? vk::ImageUsageFlagBits::eTransferSrc : vk::ImageUsageFlags{}) | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
		.sharingMode = vk::SharingMode::eExclusive,
		.initialLayout = vk::ImageLayout::eUndefined
	};

	vk::raii::Image image = renderer::App::device.createImage(info);
	const vk::MemoryRequirements memoryRequirements = image.getMemoryRequirements();

	const vk::MemoryAllocateInfo allocInfo{
		.allocationSize = memoryRequirements.size,
		.memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
	};

	vk::raii::DeviceMemory imageMemory = renderer::App::device.allocateMemory(allocInfo);
	image.bindMemory(*imageMemory, 0);

	const vk::raii::CommandBuffer commandBuffer = beginSingleTimeCommands(commandPool);
	transitionImageLayout(commandBuffer, *image, preparedUpload.mipLevels,
		vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
		{}, vk::AccessFlagBits::eTransferWrite);

	copyBufferToTextureImage(commandBuffer, stagingBuffer.getBuffer(), *image, preparedUpload.width, preparedUpload.height);
	if (preparedUpload.mipLevels > 1) {
		generateTextureMipmaps(commandBuffer, *image, preparedUpload.mipLevels, preparedUpload.width, preparedUpload.height);
	}
	else {
		transitionImageLayout(commandBuffer, *image, preparedUpload.mipLevels,
			vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
			vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead);
	}
	endSingleTimeCommands(commandBuffer);
	vk::raii::ImageView imageView = createImageView(*image, preparedUpload.mipLevels);

	return UploadedImage{
		.imageMemory = std::move(imageMemory),
		.image = std::move(image),
		.imageView = std::move(imageView),
		.mipLevels = preparedUpload.mipLevels,
		.sourceWidth = preparedUpload.sourceWidth,
		.sourceHeight = preparedUpload.sourceHeight,
		.residentMaxDimension = preparedUpload.residentMaxDimension
	};
}

void renderer::Texture::loadFromFile(const std::filesystem::path& file, uint32_t maxResidentDimension, vk::CommandPool commandPool) {
	installUploadedImage(uploadPrepared(prepareUpload(file, maxResidentDimension), commandPool));
}

void renderer::Texture::installUploadedImage(UploadedImage&& uploadedImage) {
	assert(*uploadedImage.imageMemory && *uploadedImage.image && *uploadedImage.imageView &&
		   "An uploaded texture must own complete Vulkan image resources");
	assert(uploadedImage.mipLevels > 0 && uploadedImage.sourceWidth > 0 &&
		   uploadedImage.sourceHeight > 0 && uploadedImage.residentMaxDimension > 0 &&
		   "An uploaded texture must have valid dimensions");

	if (*_image) {
		// Reserve before moving the live image so allocation failure leaves this texture unchanged.
		TextureCache::_retiredTextureImages.reserve(TextureCache::_retiredTextureImages.size() + 1);
		TextureCache::_retiredTextureImages.push_back(TextureCache::RetiredTextureImage{
			.image = UploadedImage{
				.imageMemory = std::move(_imageMemory),
				.image = std::move(_image),
				.imageView = std::move(_imageView),
				.mipLevels = _mipLevels,
				.sourceWidth = _sourceWidth,
				.sourceHeight = _sourceHeight,
				.residentMaxDimension = _residentMaxDimension },
			.retireAfterFrame = TextureCache::_streamingFrame + RetiredTextureResourceFrameDelay });
	}

	_imageMemory = std::move(uploadedImage.imageMemory);
	_image = std::move(uploadedImage.image);
	_imageView = std::move(uploadedImage.imageView);
	_mipLevels = uploadedImage.mipLevels;
	_sourceWidth = uploadedImage.sourceWidth;
	_sourceHeight = uploadedImage.sourceHeight;
	_residentMaxDimension = uploadedImage.residentMaxDimension;
	_imageVersion++;
}

bool renderer::Texture::startStreamingPrepare(uint64_t requestFrame) {
	assert(requestFrame == TextureCache::_streamingFrame && "Streaming jobs must target the current frame");
	if (_file.empty()) {
		return false;
	}

	if (hasActiveStreamingPrepare()) {
		return false;
	}

	const uint32_t targetMaxDimension = streamingTargetForFrame(requestFrame);
	if (targetMaxDimension == _residentMaxDimension) {
		return false;
	}

	_streamingPrepare = std::async(std::launch::async, [file = _file, targetMaxDimension]() {
		const PreparedUpload preparedUpload = Texture::prepareUpload(file, targetMaxDimension);
		const vk::CommandPoolCreateInfo poolInfo{
			.flags = vk::CommandPoolCreateFlagBits::eTransient,
			.queueFamilyIndex = renderer::App::queueFamily
		};
		const vk::raii::CommandPool commandPool = renderer::App::device.createCommandPool(poolInfo);
		return Texture::uploadPrepared(preparedUpload, *commandPool);
	});
	return true;
}

bool renderer::Texture::finishStreamingPrepare() {
	if (!streamingPrepareReady()) {
		return false;
	}

	UploadedImage uploadedImage = _streamingPrepare.get();

	const uint32_t currentTarget = streamingTargetForFrame(TextureCache::_streamingFrame);
	if (uploadedImage.residentMaxDimension != currentTarget) {
		return false;
	}

	if (uploadedImage.residentMaxDimension == _residentMaxDimension) {
		return false;
	}

	installUploadedImage(std::move(uploadedImage));
	TextureCache::writeBindlessTextureDescriptor(_bindlessIndex, descriptorInfo());
	_descriptorVersions[TextureCache::_activeDescriptorSetIndex] = _imageVersion;
	return true;
}

void renderer::Texture::requestMaxResidentDimension(uint32_t maxResidentDimension, uint64_t requestFrame, float priority) noexcept {
	assert(requestFrame == TextureCache::_streamingFrame && "Texture requests must belong to the current streaming frame");
	assert(std::isfinite(priority) && priority >= 0.0f && "Texture streaming priority must be finite and non-negative");
	if (_file.empty()) {
		return;
	}

	const uint32_t requestedMaxDimension = normalizedResidentMaxDimension(maxResidentDimension, _sourceWidth, _sourceHeight);
	if (_lastStreamingRequestFrame != requestFrame || requestedMaxDimension > _requestedMaxDimension) {
		_requestedMaxDimension = requestedMaxDimension;
		_streamingPriority = priority;
		_lastStreamingRequestFrame = requestFrame;
	}
	else if (requestedMaxDimension == _requestedMaxDimension) {
		_streamingPriority = std::min(_streamingPriority, priority);
	}

	if (_requestedMaxDimension < _residentMaxDimension) {
		if (_lowerRequestedMaxDimension != _requestedMaxDimension || _lowerRequestSinceFrame == 0) {
			_lowerRequestedMaxDimension = _requestedMaxDimension;
			_lowerRequestSinceFrame = requestFrame;
		}
	}
	else {
		_lowerRequestSinceFrame = 0;
	}
}

uint32_t renderer::Texture::streamingTargetForFrame(uint64_t requestFrame) const noexcept {
	return _lastStreamingRequestFrame == requestFrame
			   ? _requestedMaxDimension
			   : normalizedResidentMaxDimension(LowTextureStreamMaxDimension, _sourceWidth, _sourceHeight);
}

bool renderer::Texture::hasActiveStreamingPrepare() const noexcept {
	return _streamingPrepare.valid();
}

bool renderer::Texture::streamingPrepareReady() const {
	if (!hasActiveStreamingPrepare()) {
		return false;
	}
	return _streamingPrepare.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void renderer::Texture::createSampler() {
	const vk::SamplerCreateInfo samplerInfo{
		.magFilter = vk::Filter::eLinear,
		.minFilter = vk::Filter::eLinear,
		.mipmapMode = vk::SamplerMipmapMode::eLinear,
		.addressModeU = vk::SamplerAddressMode::eRepeat,
		.addressModeV = vk::SamplerAddressMode::eRepeat,
		.addressModeW = vk::SamplerAddressMode::eRepeat,
		.mipLodBias = 0.0f,
		.anisotropyEnable = VK_FALSE,
		.maxAnisotropy = 1.0f,
		.compareEnable = VK_FALSE,
		.compareOp = vk::CompareOp::eAlways,
		.minLod = 0.0f,
		.maxLod = VK_LOD_CLAMP_NONE,
		.borderColor = vk::BorderColor::eIntOpaqueBlack,
		.unnormalizedCoordinates = VK_FALSE
	};

	_sampler = renderer::App::device.createSampler(samplerInfo);
}

uint32_t renderer::TextureStreamer::textureResolutionForDistance(float distance) noexcept {
	assert(std::isfinite(distance) && distance >= 0.0f && "Texture streaming distance must be finite and non-negative");
	const RendererSettings& settings = Runtime::configuration().renderer;
	const float fullResolutionDistance = settings.textureFullResolutionDistance;
	const float mediumResolutionDistance = std::max(
		fullResolutionDistance, settings.textureMediumResolutionDistance);
	assert(std::isfinite(fullResolutionDistance) && fullResolutionDistance >= 0.0f &&
		   "Full-resolution streaming distance must be finite and non-negative");
	assert(std::isfinite(mediumResolutionDistance) && mediumResolutionDistance >= fullResolutionDistance &&
		   "Medium-resolution streaming distance must be finite and no smaller than the full-resolution distance");

	if (distance <= fullResolutionDistance) {
		return FullTextureResolution;
	}
	if (distance <= mediumResolutionDistance) {
		return MediumTextureStreamMaxDimension;
	}
	return LowTextureStreamMaxDimension;
}

glm::ivec2 renderer::TextureStreamer::updateStaticChunkCenter(const glm::vec2& cameraPosition) noexcept {
	assert(std::isfinite(cameraPosition.x) && std::isfinite(cameraPosition.y) &&
		   "Camera position must be finite when selecting texture streaming chunks");
	if (!_hasStaticChunkCenter) {
		_staticChunkCenter = GameObjectContainer::getStaticChunkCoords(cameraPosition);
		_hasStaticChunkCenter = true;
		return _staticChunkCenter;
	}

	constexpr float chunkSize = GameObjectContainer::getChunkSize();
	const float borderBuffer = chunkSize * StaticTextureChunkBorderBufferFraction;
	assert(chunkSize > 0.0f && borderBuffer >= 0.0f && borderBuffer < chunkSize &&
		   "Static texture chunk hysteresis requires a positive chunk and a smaller non-negative border");

	const auto updateAxis = [chunkSize, borderBuffer](float position, int center) noexcept {
		if (position < static_cast<float>(center) * chunkSize - borderBuffer) {
			return static_cast<int>(std::floor((position + borderBuffer) / chunkSize));
		}
		if (position >= static_cast<float>(center + 1) * chunkSize + borderBuffer) {
			return static_cast<int>(std::floor((position - borderBuffer) / chunkSize));
		}
		return center;
	};

	_staticChunkCenter.x = updateAxis(cameraPosition.x, _staticChunkCenter.x);
	_staticChunkCenter.y = updateAxis(cameraPosition.y, _staticChunkCenter.y);
	return _staticChunkCenter;
}

void renderer::TextureStreamer::update(const UniformBufferObject& ubo, uint32_t frameIndex, float dynamicObjectViewDistance) {
	assert(std::isfinite(dynamicObjectViewDistance) && dynamicObjectViewDistance >= 0.0f &&
		   "Dynamic object view distance must be finite and non-negative");
	TextureCache::beginStreamingFrame(frameIndex);
	_requests.clear();

	std::unordered_map<size_t, ModelStreamingRequest> modelRequests;
	const auto requestModel = [&](ModelCache::ID modelID, uint32_t maxResidentDimension, float priority) {
		const auto [request, inserted] = modelRequests.try_emplace(modelID, ModelStreamingRequest{
																				 .maxResidentDimension = maxResidentDimension,
																				 .priority = priority });
		if (!inserted) {
			request->second.maxResidentDimension = maxRequestedTextureResolution(
				request->second.maxResidentDimension,
				maxResidentDimension);
			request->second.priority = std::min(request->second.priority, priority);
		}
	};

	for (const auto id : GameObjectContainer::getDynamicGameObjects()) {
		const auto& obj = GameObjectContainer::get(id);
		const float distance = glm::length(obj.position - glm::vec3(ubo.cameraPos));
		if (distance > dynamicObjectViewDistance) {
			continue;
		}

		requestModel(obj.getModel(), textureResolutionForDistance(distance), distance);
	}

	const glm::vec2 cameraPosition{ ubo.cameraPos.x, ubo.cameraPos.z };
	const auto requestStaticChunks = [&](const std::array<GameObjectContainer::StaticChunk, 9>& chunks, bool allowFullResolutionCenter) {
		for (const auto& chunk : chunks) {
			if (chunk.objects == nullptr)
				break;
			const int chunkRing = std::max(std::abs(chunk.offset.x), std::abs(chunk.offset.y));
			const uint32_t maxResidentDimension = allowFullResolutionCenter
												  ? textureResolutionForStaticChunk(chunk)
												  : MediumTextureStreamMaxDimension;
			for (const auto id : *chunk.objects) {
				const auto& obj = GameObjectContainer::get(id);
				requestModel(obj.getModel(), maxResidentDimension, static_cast<float>(chunkRing));
			}
		}
	};

	const glm::ivec2 cameraChunkCenter = GameObjectContainer::getStaticChunkCoords(cameraPosition);
	const glm::ivec2 streamingChunkCenter = updateStaticChunkCenter(cameraPosition);
	if (cameraChunkCenter.x == streamingChunkCenter.x && cameraChunkCenter.y == streamingChunkCenter.y) {
		requestStaticChunks(GameObjectContainer::getStaticGameObjectChunks(streamingChunkCenter), true);
	}
	else {
		requestStaticChunks(GameObjectContainer::getStaticGameObjectChunks(cameraChunkCenter), false);
		requestStaticChunks(GameObjectContainer::getStaticGameObjectChunks(streamingChunkCenter), true);
	}

	for (const auto& [modelID, modelRequest] : modelRequests) {
		for (const auto& [_, textureID] : ModelCache::getModel(modelID).getTextures()) {
			if (textureID == 0) {
				continue;
			}
			_requests.push_back(TextureCache::StreamingRequest{
				.id = textureID,
				.maxResidentDimension = modelRequest.maxResidentDimension,
				.priority = modelRequest.priority });
		}
	}

	std::ranges::sort(_requests, [](const auto& left, const auto& right) noexcept {
		return left.id < right.id;
	});
	size_t writeIndex = 0;
	for (size_t readIndex = 0; readIndex < _requests.size();) {
		const TextureCache::ID textureID = _requests[readIndex].id;
		uint32_t maxResidentDimension = _requests[readIndex].maxResidentDimension;
		float priority = _requests[readIndex].priority;
		readIndex++;
		while (readIndex < _requests.size() && _requests[readIndex].id == textureID) {
			maxResidentDimension = maxRequestedTextureResolution(maxResidentDimension, _requests[readIndex].maxResidentDimension);
			priority = std::min(priority, _requests[readIndex].priority);
			readIndex++;
		}
		_requests[writeIndex++] = TextureCache::StreamingRequest{
			.id = textureID,
			.maxResidentDimension = maxResidentDimension,
			.priority = priority
		};
	}
	_requests.resize(writeIndex);
	TextureCache::requestTextureResolutions(_requests);
	TextureCache::applyStreamingRequests();
}

void TextureCache::initializeBindlessDescriptorSets(
	const std::array<vk::DescriptorSet, 2>& descriptorSets) noexcept {
	assert(std::ranges::all_of(descriptorSets, [](vk::DescriptorSet descriptorSet) noexcept { return static_cast<bool>(descriptorSet); }) &&
		   "Bindless descriptor sets must be valid");
	assert(std::ranges::all_of(_bindlessDescriptorSets, [](vk::DescriptorSet descriptorSet) noexcept { return !static_cast<bool>(descriptorSet); }) &&
		   "Bindless descriptor sets are already initialized");
	assert(_firstFreeTextureSlot == InvalidBindlessTextureIndex && "Bindless free list must be empty before initialization");

	_bindlessDescriptorSets = descriptorSets;
	_occupiedTextureSlots.fill(false);
	_firstFreeTextureSlot = 1;
}

// Large stack alloc but OK since it is only called once during startup
BindlessTextureIndex TextureCache::registerDefaultTexture(const Texture& texture) noexcept {
	assert(std::ranges::all_of(_bindlessDescriptorSets, [](vk::DescriptorSet descriptorSet) noexcept { return static_cast<bool>(descriptorSet); }) &&
		   "Bindless descriptor sets must be initialized before registering textures");
	assert(!_occupiedTextureSlots[0] && "Default texture slot is already registered");

	_defaultTextureInfo = texture.descriptorInfo();
	_occupiedTextureSlots[0] = true;

	std::array<vk::DescriptorImageInfo, MaxBindlessTextures> defaultDescriptors;
	defaultDescriptors.fill(_defaultTextureInfo);

	for (vk::DescriptorSet descriptorSet : _bindlessDescriptorSets) {
		const vk::WriteDescriptorSet descriptorWrite{
			.dstSet = descriptorSet,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = MaxBindlessTextures,
			.descriptorType = vk::DescriptorType::eCombinedImageSampler,
			.pImageInfo = defaultDescriptors.data()
		};

		renderer::App::device.updateDescriptorSets(descriptorWrite, {});
	}
	return 0;
}

BindlessTextureIndex TextureCache::reserveTextureSlot() {
	assert(std::ranges::all_of(_bindlessDescriptorSets, [](vk::DescriptorSet descriptorSet) noexcept { return static_cast<bool>(descriptorSet); }) &&
		   "Bindless descriptor sets must be initialized before registering textures");

	if (_firstFreeTextureSlot == MaxBindlessTextures) {
		throw std::runtime_error("Bindless texture descriptor array is full");
	}
	const BindlessTextureIndex index = _firstFreeTextureSlot;

	assert(index > 0 && index < MaxBindlessTextures && "First free texture slot must be a non-default slot in range");
	assert(!_occupiedTextureSlots[index] && "First free texture slot must be unused");
	_occupiedTextureSlots[index] = true;

	while (_firstFreeTextureSlot < MaxBindlessTextures && _occupiedTextureSlots[_firstFreeTextureSlot]) {
		_firstFreeTextureSlot++;
	}
	return index;
}

void TextureCache::releaseTextureSlot(BindlessTextureIndex index) noexcept {
	if (index == InvalidBindlessTextureIndex) {
		return;
	}

	assert(index != 0 && "The default texture must not be unregistered here");
	assert(index < MaxBindlessTextures && "Texture slot is outside the bindless descriptor array");
	assert(_occupiedTextureSlots[index] && "Attempted to unregister a texture slot that is not in use");

	_occupiedTextureSlots[index] = false;
	if (_firstFreeTextureSlot > index) {
		_firstFreeTextureSlot = index;
	}
}

void TextureCache::writeBindlessTextureDescriptor(
	BindlessTextureIndex index,
	const vk::DescriptorImageInfo& descriptorInfo) noexcept {
	writeTextureDescriptor(_bindlessDescriptorSets[_activeDescriptorSetIndex], index, descriptorInfo);
}

void TextureCache::writeBindlessTextureDescriptorToAllSets(
	BindlessTextureIndex index,
	const vk::DescriptorImageInfo& descriptorInfo) noexcept {
	for (vk::DescriptorSet descriptorSet : _bindlessDescriptorSets) {
		writeTextureDescriptor(descriptorSet, index, descriptorInfo);
	}
}

void TextureCache::syncActiveFrameDescriptors() noexcept {
	for (auto& [_, texture] : _cache) {
		assert(texture._bindlessIndex < MaxBindlessTextures && "File texture bindless index must be in range");
		if (texture._bindlessIndex == 0) {
			continue;
		}
		if (texture._descriptorVersions[_activeDescriptorSetIndex] == texture._imageVersion) {
			continue;
		}
		writeBindlessTextureDescriptor(texture._bindlessIndex, texture.descriptorInfo());
		texture._descriptorVersions[_activeDescriptorSetIndex] = texture._imageVersion;
	}
}

void TextureCache::collectRetiredTextureResources() noexcept {
	std::erase_if(_retiredTextureImages, [](const RetiredTextureImage& retiredTextureImage) noexcept {
		return retiredTextureImage.retireAfterFrame <= _streamingFrame;
	});
}

Texture& TextureCache::getTexture(ID id) noexcept {
	assert(_cache.contains(id) && "TextureCache does not contain a texture with given id");
	return _cache.find(id)->second;
}

void TextureCache::beginStreamingFrame(uint32_t descriptorSetIndex) noexcept {
	std::lock_guard<std::mutex> lock(_mutex);
	assert(descriptorSetIndex < _bindlessDescriptorSets.size() && "Texture descriptor set index must be in range");
	assert(_bindlessDescriptorSets[descriptorSetIndex] && "Texture descriptor set must be initialized before streaming");
	_activeDescriptorSetIndex = descriptorSetIndex;
	_streamingFrame++;
	syncActiveFrameDescriptors();
	collectRetiredTextureResources();
}

void TextureCache::requestTextureResolutions(std::span<const StreamingRequest> requests) {
	std::lock_guard<std::mutex> lock(_mutex);
	for (const auto& request : requests) {
		if (request.id == 0) {
			continue;
		}
		assert(_cache.contains(request.id) && "A streaming request must reference a loaded texture");
		_cache.find(request.id)->second.requestMaxResidentDimension(request.maxResidentDimension, _streamingFrame, request.priority);
	}
}

void TextureCache::applyStreamingRequests() {
	size_t uploads = 0;
	std::lock_guard<std::mutex> lock(_mutex);
	_upgradeCandidates.clear();

	const auto finishTexture = [&](Texture& texture) {
		if (uploads >= MaxTextureStreamUploadsPerFrame) {
			return;
		}
		try {
			if (texture.finishStreamingPrepare()) {
				uploads++;
			}
		}
		catch (const std::exception& e) {
			Runtime::log(LogLevel::warning, std::string("Texture streaming request failed: ") + e.what());
		}
	};

	size_t activePrepareJobs = 0;
	for (auto& [id, texture] : _cache) {
		if (id == 0 || !texture.hasActiveStreamingPrepare()) {
			continue;
		}
		activePrepareJobs++;
		if (texture.streamingPrepareReady()) {
			finishTexture(texture);
			if (!texture.hasActiveStreamingPrepare()) {
				activePrepareJobs--;
			}
		}
	}

	if (uploads >= MaxTextureStreamUploadsPerFrame ||
		activePrepareJobs >= MaxTextureStreamPrepareJobs) {
		return;
	}

	_upgradeCandidates.reserve(_cache.size());
	for (auto& [id, texture] : _cache) {
		if (id == 0 || texture.hasActiveStreamingPrepare()) {
			continue;
		}
		if (texture._lastStreamingRequestFrame == _streamingFrame &&
			texture._requestedMaxDimension > texture._residentMaxDimension) {
			_upgradeCandidates.push_back(&texture);
		}
	}

	std::ranges::sort(_upgradeCandidates, [](const Texture* left, const Texture* right) noexcept {
		if (left->_streamingPriority != right->_streamingPriority) {
			return left->_streamingPriority < right->_streamingPriority;
		}
		return left->_requestedMaxDimension > right->_requestedMaxDimension;
	});

	for (Texture* texture : _upgradeCandidates) {
		assert(texture != nullptr && "Upgrade candidate texture must not be null");
		if (activePrepareJobs >= MaxTextureStreamPrepareJobs) {
			break;
		}
		try {
			if (texture->startStreamingPrepare(_streamingFrame)) {
				activePrepareJobs++;
			}
		}
		catch (const std::exception& e) {
			Runtime::log(LogLevel::warning, std::string("Failed to start texture streaming request: ") + e.what());
		}
	}
	_upgradeCandidates.clear();

	if (activePrepareJobs >= MaxTextureStreamPrepareJobs ||
		_streamingFrame % TextureStreamDemotionIntervalFrames != 0) {
		return;
	}

	for (auto& [id, texture] : _cache) {
		if (activePrepareJobs >= MaxTextureStreamPrepareJobs) {
			break;
		}
		if (id == 0 || texture.hasActiveStreamingPrepare()) {
			continue;
		}

		const uint32_t targetMaxDimension = texture.streamingTargetForFrame(_streamingFrame);
		if (targetMaxDimension >= texture._residentMaxDimension) {
			continue;
		}

		const bool lowerVisibleTargetIsStable =
			texture._lastStreamingRequestFrame == _streamingFrame &&
			texture._lowerRequestSinceFrame != 0 &&
			texture._lowerRequestSinceFrame + TextureStreamDemotionDelayFrames <= _streamingFrame;
		const bool textureHasBeenUnrequested =
			texture._lastStreamingRequestFrame != _streamingFrame &&
			texture._lastStreamingRequestFrame + TextureStreamDemotionDelayFrames <= _streamingFrame;
		if (!(lowerVisibleTargetIsStable || textureHasBeenUnrequested)) {
			continue;
		}

		try {
			if (texture.startStreamingPrepare(_streamingFrame)) {
				activePrepareJobs++;
			}
		}
		catch (const std::exception& e) {
			Runtime::log(LogLevel::warning, std::string("Failed to start texture demotion request: ") + e.what());
		}
	}
}

// TODO: if one fails every copy using the failed one will point to a non existant texture and cause an
// exception in getTexture or hit the assertion as this is not how it should be
TextureCache::ID TextureCache::loadTexture(const glm::vec3& color, const std::filesystem::path& file, vk::CommandPool commandPool) {
	assert((std::filesystem::is_regular_file(file) || file.empty()) && "The texture must be a file or empty");
	static std::atomic<ID> currID = 1;
	std::lock_guard<std::mutex> loadLock(_loadMutex);

	const auto key = std::make_pair(color, file);

	ID id;
	BindlessTextureIndex bindlessIndex = InvalidBindlessTextureIndex;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (const auto idIt = _idMap.find(key); idIt != _idMap.end()) {
			const ID existingID = idIt->second;
			if (existingID == 0) {
				return 0;
			}
			assert(_refCounts.contains(existingID) && "Every texture ID-map entry must have a reference count");
			_refCounts.find(existingID)->second++;
			return existingID;
		}

		id = currID.fetch_add(1);
		bindlessIndex = file.empty() ? 0 : reserveTextureSlot();
	}

	std::optional<Texture> texture;
	try {
		if (file.empty()) {
			texture.emplace(Texture(color));
		}
		else {
			texture.emplace(Texture(file, color, bindlessIndex, commandPool));
		}

		{
			std::lock_guard<std::mutex> lock(_mutex);
			assert(!_idMap.contains(key) && "Texture ID map entry must not already exist");
			assert(!_refCounts.contains(id) && "Texture reference count must not already exist");
			assert(!_cache.contains(id) && "Texture cache entry must not already exist");
			_idMap.emplace(key, id);
			_refCounts.emplace(id, 1);
			_cache.emplace(id, std::move(*texture));
		}
	}
	catch (const std::exception& e) {
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_idMap.erase(key);
			_refCounts.erase(id);
			_cache.erase(id);

			if (texture.has_value()) {
				texture.reset();
			}
			else if (!file.empty()) {
				releaseTextureSlot(bindlessIndex);
			}
		}
		const std::string message = std::string("Failed to create texture: ") + file.string() + "\nWith: " + e.what();
		Runtime::log(LogLevel::error, message);
		throw std::runtime_error(message);
	}
	return id;
}

void TextureCache::unloadTexture(ID id) noexcept {
	assert(_cache.contains(id) && "Texture is not loaded");
	assert(_refCounts.contains(id) && "Texture does not have a ref count");
	assert(_refCounts.find(id)->second > 0 && "Texture reference count can't be 0 when unloading texture");
	assert(std::ranges::any_of(_idMap, [id](const auto& entry) noexcept {
		return entry.second == id;
	}) && "Every cached texture ID must have an ID-map entry");
	if (id == 0) {
		return;
	}

	const auto refCount = _refCounts.find(id);
	if (--refCount->second != 0) {
		return;
	}
	const auto idMapEntry = std::ranges::find_if(_idMap, [id](const auto& entry) noexcept {
		return entry.second == id;
	});
	_cache.erase(id);
	_refCounts.erase(refCount);
	_idMap.erase(idMapEntry);
}

void renderer::TextureCache::loadDefault() {
	const auto defaultKey = std::make_pair(glm::vec3{ 1.0 }, std::filesystem::path{});
	assert(!_cache.contains(0) && "Attempting to create multiple default textures");
	assert(!_idMap.contains(defaultKey) && "Default texture ID map entry must not already exist");
	assert(!_refCounts.contains(0) && "Default texture reference count must not already exist");

	try {
		_cache.emplace(0, Texture());
		_idMap.emplace(defaultKey, 0);
		_refCounts.emplace(0, 1);
	}
	catch (const std::exception& e) {
		_cache.erase(0);
		_idMap.erase(defaultKey);
		_refCounts.erase(0);
		_occupiedTextureSlots[0] = false;
		_defaultTextureInfo = {};
		Texture::resetSampler();
		// This is not recoverable
		Runtime::log(LogLevel::error, std::string("Failed to create default texture, with: ") + e.what());
		throw std::runtime_error("Failed to create default texture");
	}
}

void renderer::TextureCache::unloadDefault() noexcept {
	assert(_cache.size() == 1 && "There are still others or there are no textures loaded while unloading default");
	assert(_idMap.size() == 1 && "There are still others or there are no textures in _idMap");
	assert(_refCounts.size() == 1 && "There are still others or there are no textures in _refCounts");
	assert(_cache.contains(0) && "The remaining texture is not the default one");
	assert(std::ranges::any_of(_idMap, [](const auto& entry) noexcept {
		return entry.second == 0;
	}) && "The remaining texture is not the default one");
	assert(_refCounts.contains(0) && "The remaining texture is not the default one");
	assert(_refCounts.find(0)->second == 1 && "Default texture reference count must be 1 while unloading it");

	_occupiedTextureSlots[0] = false;
	assert(std::ranges::none_of(
			   _occupiedTextureSlots,
			   [](bool occupied) noexcept { return occupied; }) &&
		   "All bindless texture slots must be released after the default texture is unloaded");

	const auto defaultIDMapEntry = std::ranges::find_if(_idMap, [](const auto& entry) noexcept {
		return entry.second == 0;
	});

	_defaultTextureInfo = {};

	_idMap.erase(defaultIDMapEntry);
	_cache.erase(0);
	_refCounts.erase(0);
	Texture::resetSampler();
	_bindlessDescriptorSets.fill(nullptr);
	_activeDescriptorSetIndex = 0;
	_firstFreeTextureSlot = InvalidBindlessTextureIndex;
	_streamingFrame = 0;
	_retiredTextureImages.clear();
	_upgradeCandidates.clear();
}
