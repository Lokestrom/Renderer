#pragma once

#include "ImGui/imgui_impl_vulkan.h"
#include <vulkan/vulkan_raii.hpp>

#include <string>
#include <vector>

namespace vulkan {

vk::raii::CommandPool createCommandPool(uint32_t queueFamily, vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlags());
vk::raii::CommandBuffer beginSingleTimeCommands(vk::CommandPool commandPool = nullptr);
void endSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer);

uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);

vk::Format findSupportedFormat(
	const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features);

vk::raii::ShaderModule loadShaderModule(const std::string& path);

void createImageWithInfo(
	const vk::ImageCreateInfo& imageInfo,
	vk::MemoryPropertyFlags properties,
	vk::raii::Image& image,
	vk::raii::DeviceMemory& imageMemory);
}
