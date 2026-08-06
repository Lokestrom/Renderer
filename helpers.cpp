#include "helpers.hpp"

#include "VulkanApp.hpp"

#include <fstream>
#include <stdexcept>
#include <vector>

using namespace vulkan;

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

	App::submitAndWaitForFence(submitInfo);
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

vk::raii::ShaderModule vulkan::loadShaderModule(const std::string& path) {
	std::ifstream stream(path, std::ios::binary);
	if (!stream) {
		throw std::runtime_error(std::string("Could not open file: ") + path);
	}

	stream.seekg(0, std::ios_base::end);
	const std::streampos end = stream.tellg();
	if (end <= 0 || static_cast<size_t>(end) % sizeof(uint32_t) != 0) {
		throw std::runtime_error(std::string("Invalid shader file: ") + path);
	}
	const size_t size = static_cast<size_t>(end);
	stream.seekg(0, std::ios_base::beg);

	std::vector<uint32_t> buffer(size / sizeof(uint32_t));
	if (!stream.read(reinterpret_cast<char*>(buffer.data()), size)) {
		throw std::runtime_error(std::string("Could not read file: ") + path);
	}

	const vk::ShaderModuleCreateInfo shaderModuleInfo{
		.codeSize = buffer.size() * sizeof(uint32_t),
		.pCode = buffer.data()
	};
	return App::device.createShaderModule(shaderModuleInfo);
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
