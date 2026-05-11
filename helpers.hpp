#pragma once

#include "ImGui/imgui_impl_vulkan.h"

namespace vulkan {
vk::CommandBuffer beginSingleTimeCommands();
void endSingleTimeCommands(vk::CommandBuffer commandBuffer);

uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);

vk::Format findSupportedFormat(
	const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features);

void createImageWithInfo(
	const vk::ImageCreateInfo& imageInfo,
	vk::MemoryPropertyFlags properties,
	vk::Image& image,
	vk::DeviceMemory& imageMemory);

void vkCheck(vk::Result err);
}