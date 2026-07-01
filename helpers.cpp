#include "helpers.hpp"

#include "VulkanApp.hpp"

#include <mutex>

using namespace vulkan;

namespace {
static std::mutex singleCommandBufferQueueMutex;
}

vk::raii::CommandPool vulkan::createCommandPool(uint32_t queueFamily, vk::CommandPoolCreateFlags flags) {
	vk::CommandPoolCreateInfo poolInfo{
		.flags = flags,
		.queueFamilyIndex = queueFamily
	};
	return App::device.createCommandPool(poolInfo);
}

vk::raii::CommandBuffer vulkan::beginSingleTimeCommands(vk::CommandPool commandPool) {
	if (commandPool == nullptr) {
		commandPool = App::mainWindowData.Frames[0].CommandPool;
	}
	vk::CommandBufferAllocateInfo allocInfo {
		.commandPool = commandPool,
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = 1
	};

	auto commandBuffers = App::device.allocateCommandBuffers(allocInfo);
	vk::raii::CommandBuffer commandBuffer = std::move(commandBuffers.front());

	vk::CommandBufferBeginInfo beginInfo {
		.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
	};

	commandBuffer.begin(beginInfo);

	return commandBuffer;
}

void vulkan::endSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) {
	assert(*commandBuffer && "Command buffer is null");

	commandBuffer.end();

	vk::CommandBuffer rawCommandBuffer = *commandBuffer;
	vk::SubmitInfo submitInfo {
		.commandBufferCount = 1,
		.pCommandBuffers = &rawCommandBuffer
	};

	// TODO: Use fences instead of waiting for the queue to be idle, this is a bottleneck
	std::lock_guard<std::mutex> lock(singleCommandBufferQueueMutex);
	App::queue.submit(submitInfo);
	App::queue.waitIdle();
}

uint32_t vulkan::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
	vk::PhysicalDeviceMemoryProperties memProperties = App::physicalDevice.getMemoryProperties();
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
		vk::FormatProperties props = App::physicalDevice.getFormatProperties(format);

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
	vk::raii::Image& image,
	vk::raii::DeviceMemory& imageMemory) {
	image = App::device.createImage(imageInfo);

	vk::MemoryRequirements memRequirements = image.getMemoryRequirements();

	vk::MemoryAllocateInfo allocInfo{};
	allocInfo.sType = vk::StructureType::eMemoryAllocateInfo;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

	imageMemory = App::device.allocateMemory(allocInfo);

	image.bindMemory(*imageMemory, 0);
}
