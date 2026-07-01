#pragma once

#include "VulkanApp.hpp"
#include "texture.hpp"

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
	BindlessTextureIndex textureIndex;
};


class Renderer {
public:
	Renderer();
	
	Renderer(Renderer&) = delete;
	Renderer& operator=(Renderer&) = delete;

	Renderer(Renderer&&) = delete;
	Renderer& operator=(Renderer&&) = delete;

	~Renderer();

	void render(const UniformBufferObject& ubo, const uint32_t frameIndex) noexcept;
	void setActiveCommandBuffer(vk::CommandBuffer cmd) noexcept;

	void recreate();

private:
	void validateBindlessTextureLimits() const;

	void createPipeline();
	void createDescriptorLayout();
	void createDescriptorPool();
	void createDescriptorSet();

	void createRenderPass();

	void createDepthResources();

	void createUniformBuffers();

	void createFramebuffers();

private:
	vk::raii::DescriptorSetLayout _uboDescriptorSetLayout = nullptr;
	vk::raii::DescriptorSetLayout _textureDescriptorSetLayout = nullptr;
	vk::raii::RenderPass _renderPass = nullptr;
	vk::raii::PipelineLayout _layout = nullptr;
	vk::raii::Pipeline _pipeline = nullptr;

	std::array<vk::raii::DeviceMemory, 2> _depthImageMemory{ nullptr, nullptr };
	std::array<vk::raii::Image, 2> _depthImages{ nullptr, nullptr };
	std::array<vk::raii::ImageView, 2> _depthImageViews{ nullptr, nullptr };
	std::array<vk::raii::Framebuffer, 2> _frameBuffers{ nullptr, nullptr };

	std::array<std::unique_ptr<Buffer>, 2> _uniformBuffers;

	vk::raii::DescriptorPool _descriptorPool = nullptr;
	std::array<vk::raii::DescriptorSet, 2> _uboDescriptorSets{ nullptr, nullptr };
	vk::raii::DescriptorSet _textureDescriptorSet = nullptr;

	vk::Format _depthFormat;
	vk::CommandBuffer _activeCommandBuffer;

	ModelCache::ID _vectorArrowID;
	ModelCache::ID _pointID;
};

}
