#include "helpers.hpp"

#include "VulkanApp.hpp"

using namespace vulkan;

vk::CommandBuffer vulkan::beginSingleTimeCommands() {
	vk::CommandBufferAllocateInfo allocInfo{};
	allocInfo.level = vk::CommandBufferLevel::ePrimary;
	// this is a hack in the future the vulkan::App should be totally decupled from imgui
	allocInfo.commandPool = App::mainWindowData.Frames[0].CommandPool;
	allocInfo.commandBufferCount = 1;

	vk::CommandBuffer commandBuffer;
	if (App::device.allocateCommandBuffers(&allocInfo, &commandBuffer) != vk::Result::eSuccess) {
		throw std::runtime_error("Failed to allocate a single time command buffer");
	}

	vk::CommandBufferBeginInfo beginInfo{};
	vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	if (commandBuffer.begin(&beginInfo) != vk::Result::eSuccess) {
		throw std::runtime_error("Failed to begin single time command buffer");
	}

	return commandBuffer;
}

void vulkan::endSingleTimeCommands(vk::CommandBuffer commandBuffer) {
	commandBuffer.end();

	vk::SubmitInfo submitInfo{};
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	App::queue.submit(submitInfo);
	App::queue.waitIdle();

	App::device.freeCommandBuffers(App::mainWindowData.Frames[0].CommandPool, commandBuffer);
}

uint32_t vulkan::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
	vk::PhysicalDeviceMemoryProperties memProperties;
	App::physicalDevice.getMemoryProperties(&memProperties);
	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) &&
			(memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	throw std::runtime_error("failed to find suitable memory type!");
}

vk::Format vulkan::findSupportedFormat(
	const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) {
	for (vk::Format format : candidates) {
		vk::FormatProperties props;
		App::physicalDevice.getFormatProperties(format, &props);

		if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) {
			return format;
		}
		else if (
			tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) {
			return format;
		}
	}
	throw std::runtime_error("failed to find supported format!");
}

void vulkan::createImageWithInfo(
	const vk::ImageCreateInfo& imageInfo,
	vk::MemoryPropertyFlags properties,
	vk::Image& image,
	vk::DeviceMemory& imageMemory) {
	if (App::device.createImage(&imageInfo, nullptr, &image) != vk::Result::eSuccess) {
		throw std::runtime_error("failed to create image!");
	}

	vk::MemoryRequirements memRequirements;
	App::device.getImageMemoryRequirements(image, &memRequirements);

	vk::MemoryAllocateInfo allocInfo{};
	allocInfo.sType = vk::StructureType::eMemoryAllocateInfo;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

	if (App::device.allocateMemory(&allocInfo, nullptr, &imageMemory) != vk::Result::eSuccess) {
		throw std::runtime_error("failed to allocate image memory!");
	}

	App::device.bindImageMemory(image, imageMemory, 0);
}

void vulkan::vkCheck(vk::Result err) {
	if (err == vk::Result::eSuccess)
		return;
	fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
	if (err < vk::Result::eSuccess)
		abort();
}