#pragma once


#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <array>

namespace renderer {

struct UniformBufferObject;
class GameObject;
class Model3D;

class ShadowRenderer {
public:
	static constexpr uint32_t Resolution = 2048;
	static constexpr size_t FrameCount = 2;
	static constexpr size_t CascadeCount = 3;

	ShadowRenderer() = default;

	ShadowRenderer(ShadowRenderer&) = delete;
	ShadowRenderer& operator=(ShadowRenderer&) = delete;

	ShadowRenderer(ShadowRenderer&&) = delete;
	ShadowRenderer& operator=(ShadowRenderer&&) = delete;

	void initialize(vk::PipelineLayout layout);
	void render(
		vk::CommandBuffer cmd,
		const UniformBufferObject& ubo,
		uint32_t frameIndex,
		vk::DescriptorSet uboDescriptorSet) noexcept;

	[[nodiscard]]
	void updateCascadeUniforms(UniformBufferObject& ubo) noexcept;
	[[nodiscard]]
	bool isEnabled() noexcept;
	[[nodiscard]]
	vk::DescriptorImageInfo getDescriptorImageInfo(uint32_t frameIndex) const noexcept;

private:
	void createResources();
	void createRenderPass();
	void createPipeline();
	void drawShadowCasters(
		vk::CommandBuffer cmd,
		const UniformBufferObject& ubo,
		const glm::vec4& casterBounds,
		uint32_t cascadeIndex) noexcept;
	[[nodiscard]]
	bool isShadowCasterVisible(
		const GameObject& object,
		const Model3D& model,
		const glm::mat4& lightViewProjection) noexcept;

private:
	vk::PipelineLayout _layout = nullptr;
	vk::Format _depthFormat;

	vk::raii::RenderPass _renderPass = nullptr;
	vk::raii::Pipeline _pipeline = nullptr;
	vk::raii::Sampler _sampler = nullptr;

	std::array<vk::raii::DeviceMemory, FrameCount> _imageMemory{ nullptr, nullptr };
	std::array<vk::raii::Image, FrameCount> _images{ nullptr, nullptr };
	std::array<vk::raii::ImageView, FrameCount> _imageViews{ nullptr, nullptr };
	std::array<std::array<vk::raii::ImageView, CascadeCount>, FrameCount> _cascadeImageViews{
		std::array<vk::raii::ImageView, CascadeCount>{ nullptr, nullptr, nullptr },
		std::array<vk::raii::ImageView, CascadeCount>{ nullptr, nullptr, nullptr }
	};
	std::array<std::array<vk::raii::Framebuffer, CascadeCount>, FrameCount> _frameBuffers{
		std::array<vk::raii::Framebuffer, CascadeCount>{ nullptr, nullptr, nullptr },
		std::array<vk::raii::Framebuffer, CascadeCount>{ nullptr, nullptr, nullptr }
	};

};

}
