#pragma once

#include "VulkanApp.hpp"

#include <glm/glm.hpp>

#include <array>

#include "gameObject.hpp"
#include "Buffer.hpp"
#include "Frames.hpp"

namespace vulkan {

struct UniformBufferObject {
	glm::mat4 proj;
	glm::mat4 view;
	glm::vec4 cameraPos;
	glm::vec4 lightSource;
};

struct VertexPushConstant {
	glm::mat4 modelMatrix;
};
struct FragmentPushConstant {
	glm::vec4 color;
};


class Renderer {
	friend Texture;

public:
	Renderer();
	
	Renderer(Renderer&) = delete;
	Renderer& operator=(Renderer&) = delete;

	Renderer(Renderer&&) = delete;
	Renderer& operator=(Renderer&&) = delete;

	~Renderer();

	void render(UniformBufferObject& ubo, uint32_t frameIndex);
	void setActiveCommandBuffer(vk::CommandBuffer cmd);

	void recreate();

private:
	void createPipeline();
	void createDescriptorLayout();
	void createDescriptorPool();
	void createDescriptorSet();

	void createRenderPass();

	void createDepthResources();

	void createUniformBuffers();

	void createFramebuffers();

private:
	vk::Pipeline _pipeline;
	vk::PipelineLayout _layout;
	vk::RenderPass _renderPass;

	std::array<vk::Framebuffer, 2> _frameBuffers;
	
	std::array<vk::Image, 2> _depthImages;
	std::array<vk::ImageView, 2> _depthImageViews;
	std::array<vk::DeviceMemory, 2> _depthImageMemory;

	vk::Format _depthFormat;

	vk::DescriptorSetLayout _uboDescriptorSetLayout;
	vk::DescriptorSetLayout _textureDescriptorSetLayout;
	vk::DescriptorPool _descriptorPool;
	std::array<vk::DescriptorSet, 2> _descriptorSets;

	std::array<std::unique_ptr<Buffer>, 2> _uniformBuffers;

	vk::CommandBuffer _activeCommandBuffer;

	ModelCache::ID _vectorArrowID;
	ModelCache::ID _pointID;
};

}