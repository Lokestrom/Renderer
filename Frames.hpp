#pragma once

#include "VulkanApp.hpp"

class Frame {


private:
	vk::Framebuffer buffer;
	vk::DeviceMemory frameBufferMemory;
	vk::Image _colorImage;
	vk::Image _depthImage;
};