#pragma once

#include "ImGui/imgui_impl_vulkan.h"

#include <source_location>

namespace vulkan {

vk::CommandPool createCommandPool(uint32_t queueFamily, vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlags());
vk::CommandBuffer beginSingleTimeCommands(vk::CommandPool commandPool = nullptr);
void endSingleTimeCommands(vk::CommandBuffer commandBuffer, vk::CommandPool commandPool = nullptr);

uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);

vk::Format findSupportedFormat(
	const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features);

void createImageWithInfo(
	const vk::ImageCreateInfo& imageInfo,
	vk::MemoryPropertyFlags properties,
	vk::Image& image,
	vk::DeviceMemory& imageMemory);

void vkCheck(vk::Result err, std::string_view message = "", const std::source_location& location = std::source_location::current());
}