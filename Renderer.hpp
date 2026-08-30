#pragma once

#include "texture.hpp"
#include "Model.hpp"

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <vector>

#include "Buffer.hpp"
#include "Frames.hpp"
#include "ShadowRenderer.hpp"

namespace renderer {

struct UniformBufferObject {
	glm::mat4 proj;
	glm::mat4 view;
	glm::vec4 cameraPos;
	glm::vec4 lightSource;
	std::array<glm::mat4, ShadowRenderer::CascadeCount> lightViewProjections;
	glm::vec4 shadowCascadeSplits; // w enables shadows
};

struct VertexPushConstant {
	glm::mat4 modelMatrix;
	glm::vec4 shadowData;
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

	void render(
		const UniformBufferObject& ubo,
		uint32_t frameIndex,
		uint32_t imageIndex,
		bool drawScene = true) noexcept;
	void setActiveCommandBuffer(vk::CommandBuffer cmd) noexcept;

	void releaseSwapChainResources() noexcept;
	void recreateSwapChainResources(bool formatChanged);

private:
	void validateBindlessTextureLimits() const;

	void createPipeline(bool createLayout = true);
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

	std::vector<vk::raii::DeviceMemory> _depthImageMemory;
	std::vector<vk::raii::Image> _depthImages;
	std::vector<vk::raii::ImageView> _depthImageViews;
	std::vector<vk::raii::Framebuffer> _frameBuffers;
	ShadowRenderer _shadowRenderer;

	std::array<Buffer, 2> _uniformBuffers;

	vk::raii::DescriptorPool _descriptorPool = nullptr;
	std::array<vk::raii::DescriptorSet, 2> _uboDescriptorSets{ nullptr, nullptr };
	std::array<vk::raii::DescriptorSet, 2> _textureDescriptorSets{ nullptr, nullptr };

	vk::Format _depthFormat;
	vk::CommandBuffer _activeCommandBuffer;

	TextureStreamer _textureStreamer;

};

}
