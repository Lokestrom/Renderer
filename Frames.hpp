#pragma once

#include "VulkanApp.hpp"

class Frame {
private:
	vk::raii::DeviceMemory frameBufferMemory = nullptr;
	vk::raii::Image _colorImage = nullptr;
	vk::raii::Image _depthImage = nullptr;
	vk::raii::Framebuffer buffer = nullptr;
};
