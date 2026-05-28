#include "helpers.hpp"

#include "VulkanApp.hpp"

#include "../console.hpp"

#include <mutex>

using namespace vulkan;

namespace {
static std::mutex singleCommandBufferQueueMutex;
}

vk::CommandPool vulkan::createCommandPool(uint32_t queueFamily, vk::CommandPoolCreateFlags flags) {
	vk::CommandPoolCreateInfo poolInfo{
		.flags = flags,
		.queueFamilyIndex = queueFamily
	};
	vk::CommandPool commandPool;
	vkCheck(App::device.createCommandPool(&poolInfo, nullptr, &commandPool));
	return commandPool;
}

vk::CommandBuffer vulkan::beginSingleTimeCommands(vk::CommandPool commandPool) {
	if (commandPool == nullptr) {
		commandPool = App::mainWindowData.Frames[0].CommandPool;
	}
	vk::CommandBufferAllocateInfo allocInfo {
		.commandPool = commandPool,
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = 1
	};

	vk::CommandBuffer commandBuffer;
	if (App::device.allocateCommandBuffers(&allocInfo, &commandBuffer) != vk::Result::eSuccess) {
		throw std::runtime_error("Failed to allocate a single time command buffer");
	}

	vk::CommandBufferBeginInfo beginInfo {
		.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
	};

	if (commandBuffer.begin(&beginInfo) != vk::Result::eSuccess) {
		throw std::runtime_error("Failed to begin single time command buffer");
	}

	return commandBuffer;
}

void vulkan::endSingleTimeCommands(vk::CommandBuffer commandBuffer, vk::CommandPool commandPool) {
	assert(commandBuffer && "Command buffer is null");
	if (commandPool == nullptr) {
		commandPool = App::mainWindowData.Frames[0].CommandPool;
	}

	commandBuffer.end();

	vk::SubmitInfo submitInfo {
		.commandBufferCount = 1,
		.pCommandBuffers = &commandBuffer
	};

	// TODO: Use fences instead of waiting for the queue to be idle, this is a bottleneck
	std::lock_guard<std::mutex> lock(singleCommandBufferQueueMutex);
	App::queue.submit(submitInfo);
	App::queue.waitIdle();

	App::device.freeCommandBuffers(commandPool, commandBuffer);
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
		throw std::runtime_error("failed to create image");
	}

	vk::MemoryRequirements memRequirements;
	App::device.getImageMemoryRequirements(image, &memRequirements);

	vk::MemoryAllocateInfo allocInfo{};
	allocInfo.sType = vk::StructureType::eMemoryAllocateInfo;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

	if (App::device.allocateMemory(&allocInfo, nullptr, &imageMemory) != vk::Result::eSuccess) {
		throw std::runtime_error("failed to allocate image memory");
	}

	App::device.bindImageMemory(image, imageMemory, 0);
}

void vulkan::vkCheck(vk::Result err, std::string_view message, const std::source_location& location) {
	if (err == vk::Result::eSuccess)
		return;
	__debugbreak();
	std::string completeMessage = "Vulkan error: " + vk::to_string(err) + ", message: " + std::string(message) + " at " +
		std::string(location.file_name()) + ":" + std::to_string(location.line()) + " - " + std::string(location.function_name());
	if (err > vk::Result::eSuccess)
		Console::log(Console::Log::Type::warning, completeMessage);
	else
		throw std::runtime_error("Critical vulkan error: " + completeMessage);
}