#include "ShadowRenderer.hpp"

#include "Renderer.hpp"
#include "gameObject.hpp"
#include "helpers.hpp"

#include "../App.hpp"
#include "../SettingNames.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_access.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace vulkan {

namespace {

constexpr float CascadeSplitLambda = 0.65f;
constexpr float MinimumNonZeroMagnitude = 0.000001f;
constexpr float MinimumLightDirectionLengthSquared = 0.000001f;
constexpr float CascadeRadiusQuantization = 16.0f;
constexpr float MinimumCascadeRadius = 0.1f;
constexpr float LightDirectionUpAlignmentThreshold = 0.99f;
constexpr float ShadowNearPlane = 0.1f;
constexpr float DepthBiasConstantFactor = 0.5f;
constexpr float DepthBiasSlopeFactor = 0.5f;

glm::vec3 getViewPosition(const glm::mat4& inverseProjection, const glm::vec4& clipPosition) noexcept {
	glm::vec4 viewPosition = inverseProjection * clipPosition;
	assert(glm::abs(viewPosition.w) > MinimumNonZeroMagnitude && "Cascade frustum point must have a valid homogeneous coordinate");
	viewPosition /= viewPosition.w;
	return glm::vec3(viewPosition);
}

std::array<glm::vec3, 8> getFrustumSliceCorners(
	const UniformBufferObject& ubo,
	float nearDistance,
	float farDistance) noexcept {
	const glm::mat4 inverseProjection = glm::inverse(ubo.proj);
	const glm::mat4 inverseView = glm::inverse(ubo.view);
	std::array<glm::vec3, 8> corners{};

	size_t cornerIndex = 0;
	for (float x : { -1.0f, 1.0f }) {
		for (float y : { -1.0f, 1.0f }) {
			const glm::vec3 viewRay = getViewPosition(
				inverseProjection,
				glm::vec4(x, y, 0.5f, 1.0f));
			assert(glm::abs(viewRay.z) > MinimumNonZeroMagnitude && "Cascade frustum ray must have a valid depth");

			const glm::vec3 sliceNear = viewRay * (nearDistance / viewRay.z);
			const glm::vec3 sliceFar = viewRay * (farDistance / viewRay.z);
			corners[cornerIndex] = glm::vec3(inverseView * glm::vec4(sliceNear, 1.0f));
			corners[cornerIndex + 4] = glm::vec3(inverseView * glm::vec4(sliceFar, 1.0f));
			++cornerIndex;
		}
	}
	return corners;
}

glm::mat4 getCascadeViewProjection(
	const std::array<glm::vec3, 8>& corners,
	const glm::vec3& lightDirection,
	float casterExtrusionDistance) noexcept {
	glm::vec3 center{ 0.0f };
	for (const glm::vec3& corner : corners) {
		center += corner;
	}
	center /= static_cast<float>(corners.size());

	float radius = 0.0f;
	for (const glm::vec3& corner : corners) {
		radius = std::max(radius, glm::distance(center, corner));
	}
	radius = std::max(
		std::ceil(radius * CascadeRadiusQuantization) / CascadeRadiusQuantization,
		MinimumCascadeRadius);

	const glm::vec3 up = glm::abs(glm::dot(lightDirection, glm::vec3(0.0f, 1.0f, 0.0f))) > LightDirectionUpAlignmentThreshold
		? glm::vec3(0.0f, 0.0f, 1.0f)
		: glm::vec3(0.0f, 1.0f, 0.0f);
	const glm::mat4 lightView = glm::lookAtRH(
		center - lightDirection * (radius + casterExtrusionDistance),
		center,
		up);
	glm::mat4 lightProjection = glm::orthoRH_ZO(
		-radius, radius,
		-radius, radius,
		ShadowNearPlane, radius * 2.0f + casterExtrusionDistance);

	glm::vec4 shadowOrigin = lightProjection * lightView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	shadowOrigin *= static_cast<float>(ShadowRenderer::Resolution) * 0.5f;
	const glm::vec4 roundedOrigin = glm::round(shadowOrigin);
	glm::vec4 roundingOffset = roundedOrigin - shadowOrigin;
	roundingOffset *= 2.0f / static_cast<float>(ShadowRenderer::Resolution);
	roundingOffset.z = 0.0f;
	roundingOffset.w = 0.0f;
	lightProjection[3] += roundingOffset;

	return lightProjection * lightView;
}

glm::vec4 getShadowVolumeBounds(const glm::mat4& lightViewProjection) noexcept {
	const glm::mat4 inverseLightViewProjection = glm::inverse(lightViewProjection);
	glm::vec2 minimum{ std::numeric_limits<float>::max() };
	glm::vec2 maximum{ std::numeric_limits<float>::lowest() };
	for (float x : { -1.0f, 1.0f }) {
		for (float y : { -1.0f, 1.0f }) {
			for (float z : { 0.0f, 1.0f }) {
				glm::vec4 worldPosition = inverseLightViewProjection * glm::vec4(x, y, z, 1.0f);
				assert(glm::abs(worldPosition.w) > MinimumNonZeroMagnitude &&
					"Shadow volume point must have a valid homogeneous coordinate");
				worldPosition /= worldPosition.w;
				const glm::vec2 horizontalPosition{ worldPosition.x, worldPosition.z };
				minimum = glm::min(minimum, horizontalPosition);
				maximum = glm::max(maximum, horizontalPosition);
			}
		}
	}
	return { minimum.x, minimum.y, maximum.x, maximum.y };
}

}

ShadowRenderer::ShadowRenderer()
	: _shadowDistance(::App::settings.get(settingNames::categories::rendering)
		.get<float>(settingNames::rendering::shadowDistance).getHandle())
	, _shadowsEnabled(::App::settings.get(settingNames::categories::rendering)
		.get<bool>(settingNames::rendering::shadowsEnabled).getHandle()) {
}

void ShadowRenderer::initialize(vk::PipelineLayout layout) {
	assert(layout && "Shadow renderer requires a valid pipeline layout");
	_layout = layout;
	createResources();
	createRenderPass();
	createPipeline();
}

void ShadowRenderer::updateCascadeUniforms(UniformBufferObject& ubo) noexcept {

	ubo.lightViewProjections.fill(glm::mat4(1.0f));
	ubo.shadowCascadeSplits = glm::vec4(0.0f);
	if (!isEnabled() || glm::length2(glm::vec3(ubo.lightSource)) < MinimumLightDirectionLengthSquared) {
		return;
	}
	const glm::mat4 inverseProjection = glm::inverse(ubo.proj);
	const float cameraNear = getViewPosition(inverseProjection, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)).z;
	const float shadowFar = _shadowDistance.get();
	assert(cameraNear > 0.0f && shadowFar > cameraNear && "Shadow cascades require a valid camera depth range");

	for (size_t i = 0; i < CascadeCount; ++i) {
		const float fraction = static_cast<float>(i + 1) / static_cast<float>(CascadeCount);
		const float logarithmicSplit = cameraNear * std::pow(shadowFar / cameraNear, fraction);
		const float uniformSplit = cameraNear + (shadowFar - cameraNear) * fraction;
		ubo.shadowCascadeSplits[i] = glm::mix(uniformSplit, logarithmicSplit, CascadeSplitLambda);
	}
	ubo.shadowCascadeSplits[CascadeCount - 1] = shadowFar;
	ubo.shadowCascadeSplits.w = 1.0f; // Enable shadows

	const glm::vec3 lightDirection = glm::normalize(glm::vec3(ubo.lightSource));
	float cascadeNear = cameraNear;
	for (size_t i = 0; i < CascadeCount; ++i) {
		const float cascadeFar = ubo.shadowCascadeSplits[i];
		ubo.lightViewProjections[i] = getCascadeViewProjection(
			getFrustumSliceCorners(ubo, cascadeNear, cascadeFar),
			lightDirection,
			shadowFar);
		cascadeNear = cascadeFar;
	}
}

bool ShadowRenderer::isEnabled() noexcept {
	return _shadowsEnabled.get();
}

vk::DescriptorImageInfo ShadowRenderer::getDescriptorImageInfo(uint32_t frameIndex) const noexcept {
	assert(frameIndex < _imageViews.size() && "Frame index is outside the shadow image array");
	return {
		.sampler = *_sampler,
		.imageView = *_imageViews[frameIndex],
		.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal
	};
}

void ShadowRenderer::render(
	vk::CommandBuffer cmd,
	const UniformBufferObject& ubo,
	uint32_t frameIndex,
	vk::DescriptorSet uboDescriptorSet) noexcept {
	assert(frameIndex < _frameBuffers.size() && "Frame index is outside the shadow framebuffer array");
	if (!isEnabled() || glm::length2(glm::vec3(ubo.lightSource)) < MinimumLightDirectionLengthSquared) {
		return;
	}

	const vk::Viewport viewport{
		.width = static_cast<float>(Resolution),
		.height = static_cast<float>(Resolution),
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	const vk::Rect2D scissor{ .extent = { Resolution, Resolution } };
	const vk::ClearValue clearValue{ .depthStencil = vk::ClearDepthStencilValue(1.0f, 0) };

	for (uint32_t cascadeIndex = 0; cascadeIndex < CascadeCount; ++cascadeIndex) {
		const vk::RenderPassBeginInfo info{
			.renderPass = *_renderPass,
			.framebuffer = *_frameBuffers[frameIndex][cascadeIndex],
			.renderArea = { .extent = { Resolution, Resolution } },
			.clearValueCount = 1,
			.pClearValues = &clearValue
		};

		cmd.beginRenderPass(info, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *_pipeline);
		cmd.setViewport(0, viewport);
		cmd.setScissor(0, scissor);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, _layout, 0, uboDescriptorSet, {});
		drawShadowCasters(cmd, ubo, getShadowVolumeBounds(ubo.lightViewProjections[cascadeIndex]), cascadeIndex);
		cmd.endRenderPass();
	}
}

void ShadowRenderer::drawShadowCasters(
	vk::CommandBuffer cmd,
	const UniformBufferObject& ubo,
	const glm::vec4& casterBounds,
	uint32_t cascadeIndex) noexcept {
	assert(cascadeIndex < CascadeCount && "Shadow cascade index is out of range");
	VertexPushConstant vertexPush{};
	vertexPush.shadowData.x = static_cast<float>(cascadeIndex);
	const glm::mat4& lightViewProjection = ubo.lightViewProjections[cascadeIndex];
	for (auto& id : GameObjectContainer::getDynamicGameObjects()) {
		auto& obj = GameObjectContainer::get(id);
		Model3D& model = ModelCache::getModel(obj.getModel());
		vertexPush.modelMatrix = obj.getTransformMatrix();
		if (!isShadowCasterVisible(obj, model, lightViewProjection)) {
			continue;
		}
		cmd.pushConstants(_layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(VertexPushConstant), &vertexPush);
		model.drawGeometry(cmd);
	}

	const std::vector<GameObjectContainer::StaticChunk> staticChunks =
		GameObjectContainer::getStaticGameObjectChunks(
			glm::vec2{ casterBounds.x, casterBounds.y },
			glm::vec2{ casterBounds.z, casterBounds.w });
	for (const auto& chunk : staticChunks) {
		for (auto& id : *chunk.objects) {
			auto& obj = GameObjectContainer::get(id);
			Model3D& model = ModelCache::getModel(obj.getModel());
			vertexPush.modelMatrix = obj.getTransformMatrix();
			if (!isShadowCasterVisible(obj, model, lightViewProjection)) {
				continue;
			}
			cmd.pushConstants(_layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(VertexPushConstant), &vertexPush);
			model.drawGeometry(cmd);
		}
	}
}

bool ShadowRenderer::isShadowCasterVisible(
	const GameObject& object,
	const Model3D& model,
	const glm::mat4& lightViewProjection) noexcept {
	const Model3D::BoundingSphere bounds = object.getWorldBoundingSphere(model);
	const std::array planes = {
		glm::row(lightViewProjection, 3) + glm::row(lightViewProjection, 0),
		glm::row(lightViewProjection, 3) - glm::row(lightViewProjection, 0),
		glm::row(lightViewProjection, 3) + glm::row(lightViewProjection, 1),
		glm::row(lightViewProjection, 3) - glm::row(lightViewProjection, 1),
		glm::row(lightViewProjection, 2),
		glm::row(lightViewProjection, 3) - glm::row(lightViewProjection, 2)
	};
	for (const glm::vec4& plane : planes) {
		const float normalLength = glm::length(glm::vec3(plane));
		assert(normalLength > MinimumNonZeroMagnitude && "Shadow cascade plane must have a valid normal");
		if (glm::dot(glm::vec3(plane), bounds.center) + plane.w < -bounds.radius * normalLength) {
			return false;
		}
	}
	return true;
}

void ShadowRenderer::createResources() {
	_depthFormat = findSupportedFormat(
		{ vk::Format::eD32Sfloat, vk::Format::eD16Unorm },
		vk::ImageTiling::eOptimal,
		vk::FormatFeatureFlagBits::eDepthStencilAttachment | vk::FormatFeatureFlagBits::eSampledImage);

	for (size_t i = 0; i < _images.size(); ++i) {
		const vk::ImageCreateInfo imageInfo{
			.imageType = vk::ImageType::e2D,
			.format = _depthFormat,
			.extent = { Resolution, Resolution, 1 },
			.mipLevels = 1,
			.arrayLayers = static_cast<uint32_t>(CascadeCount),
			.samples = vk::SampleCountFlagBits::e1,
			.tiling = vk::ImageTiling::eOptimal,
			.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
			.sharingMode = vk::SharingMode::eExclusive,
			.initialLayout = vk::ImageLayout::eUndefined
		};
		createImageWithInfo(
			imageInfo,
			vk::MemoryPropertyFlagBits::eDeviceLocal,
			_images[i],
			_imageMemory[i]);

		const vk::ImageViewCreateInfo arrayViewInfo{
			.image = *_images[i],
			.viewType = vk::ImageViewType::e2DArray,
			.format = _depthFormat,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eDepth,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = static_cast<uint32_t>(CascadeCount)
			}
		};
		_imageViews[i] = App::device.createImageView(arrayViewInfo);

		for (uint32_t cascadeIndex = 0; cascadeIndex < CascadeCount; ++cascadeIndex) {
			const vk::ImageViewCreateInfo cascadeViewInfo{
				.image = *_images[i],
				.viewType = vk::ImageViewType::e2D,
				.format = _depthFormat,
				.subresourceRange = {
					.aspectMask = vk::ImageAspectFlagBits::eDepth,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = cascadeIndex,
					.layerCount = 1
				}
			};
			_cascadeImageViews[i][cascadeIndex] = App::device.createImageView(cascadeViewInfo);
		}
	}

	std::array<vk::ImageMemoryBarrier, FrameCount> layoutBarriers{};
	for (size_t i = 0; i < layoutBarriers.size(); ++i) {
		layoutBarriers[i] = {
			.dstAccessMask = vk::AccessFlagBits::eShaderRead,
			.oldLayout = vk::ImageLayout::eUndefined,
			.newLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = *_images[i],
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eDepth,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = static_cast<uint32_t>(CascadeCount)
			}
		};
	}
	const vk::raii::CommandBuffer commandBuffer = beginSingleTimeCommands();
	commandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe,
		vk::PipelineStageFlagBits::eFragmentShader,
		{},
		{},
		{},
		layoutBarriers);
	endSingleTimeCommands(commandBuffer);

	const vk::SamplerCreateInfo samplerInfo{
		.magFilter = vk::Filter::eNearest,
		.minFilter = vk::Filter::eNearest,
		.mipmapMode = vk::SamplerMipmapMode::eNearest,
		.addressModeU = vk::SamplerAddressMode::eClampToBorder,
		.addressModeV = vk::SamplerAddressMode::eClampToBorder,
		.addressModeW = vk::SamplerAddressMode::eClampToBorder,
		.mipLodBias = 0.0f,
		.anisotropyEnable = VK_FALSE,
		.maxAnisotropy = 1.0f,
		.compareEnable = VK_TRUE,
		.compareOp = vk::CompareOp::eLessOrEqual,
		.minLod = 0.0f,
		.maxLod = 0.0f,
		.borderColor = vk::BorderColor::eFloatOpaqueWhite,
		.unnormalizedCoordinates = VK_FALSE
	};
	_sampler = App::device.createSampler(samplerInfo);
}

void ShadowRenderer::createRenderPass() {
	const vk::AttachmentDescription depthAttachment{
		.format = _depthFormat,
		.samples = vk::SampleCountFlagBits::e1,
		.loadOp = vk::AttachmentLoadOp::eClear,
		.storeOp = vk::AttachmentStoreOp::eStore,
		.stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
		.stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
		.initialLayout = vk::ImageLayout::eUndefined,
		.finalLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal
	};
	const vk::AttachmentReference depthReference{
		.attachment = 0,
		.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal
	};
	const vk::SubpassDescription subpass{
		.pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
		.pDepthStencilAttachment = &depthReference
	};
	const std::array dependencies = {
		vk::SubpassDependency{
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = vk::PipelineStageFlagBits::eFragmentShader,
			.dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests,
			.srcAccessMask = vk::AccessFlagBits::eShaderRead,
			.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			.dependencyFlags = vk::DependencyFlagBits::eByRegion
		},
		vk::SubpassDependency{
			.srcSubpass = 0,
			.dstSubpass = VK_SUBPASS_EXTERNAL,
			.srcStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests |
				vk::PipelineStageFlagBits::eLateFragmentTests,
			.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader,
			.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			.dstAccessMask = vk::AccessFlagBits::eShaderRead,
			.dependencyFlags = vk::DependencyFlagBits::eByRegion
		}
	};
	const vk::RenderPassCreateInfo renderPassInfo{
		.attachmentCount = 1,
		.pAttachments = &depthAttachment,
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = static_cast<uint32_t>(dependencies.size()),
		.pDependencies = dependencies.data()
	};
	_renderPass = App::device.createRenderPass(renderPassInfo);

	for (size_t frameIndex = 0; frameIndex < _frameBuffers.size(); ++frameIndex) {
		for (size_t cascadeIndex = 0; cascadeIndex < CascadeCount; ++cascadeIndex) {
			const vk::ImageView attachment = *_cascadeImageViews[frameIndex][cascadeIndex];
			const vk::FramebufferCreateInfo framebufferInfo{
				.renderPass = *_renderPass,
				.attachmentCount = 1,
				.pAttachments = &attachment,
				.width = Resolution,
				.height = Resolution,
				.layers = 1
			};
			_frameBuffers[frameIndex][cascadeIndex] = App::device.createFramebuffer(framebufferInfo);
		}
	}
}

void ShadowRenderer::createPipeline() {
	const auto bindingDescription = Model3D::Vertex::bindingDescriptions();
	const auto attributeDescriptions = Model3D::Vertex::attributeDescriptions();
	const vk::PipelineVertexInputStateCreateInfo vertexInput{
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = bindingDescription.data(),
		.vertexAttributeDescriptionCount = 1,
		.pVertexAttributeDescriptions = attributeDescriptions.data()
	};
	const vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
		.topology = vk::PrimitiveTopology::eTriangleList,
		.primitiveRestartEnable = VK_FALSE
	};
	const vk::PipelineRasterizationStateCreateInfo raster{
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = vk::PolygonMode::eFill,
		.cullMode = vk::CullModeFlagBits::eBack,
		.frontFace = vk::FrontFace::eCounterClockwise,
		.depthBiasEnable = VK_TRUE,
		.depthBiasConstantFactor = DepthBiasConstantFactor,
		.depthBiasClamp = 0.0f,
		.depthBiasSlopeFactor = DepthBiasSlopeFactor,
		.lineWidth = 1.0f
	};
	const std::array dynamicStates = {
		vk::DynamicState::eViewport,
		vk::DynamicState::eScissor
	};
	const vk::PipelineDynamicStateCreateInfo dynamicState{
		.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
		.pDynamicStates = dynamicStates.data()
	};
	const vk::PipelineViewportStateCreateInfo viewport{
		.viewportCount = 1,
		.scissorCount = 1
	};
	const vk::PipelineDepthStencilStateCreateInfo depthStencil{
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = vk::CompareOp::eLessOrEqual
	};
	const vk::PipelineMultisampleStateCreateInfo multisample{
		.rasterizationSamples = vk::SampleCountFlagBits::e1
	};
	const vk::PipelineColorBlendStateCreateInfo blend{};

	vk::raii::ShaderModule vertexShader = loadShaderModule(SHADER_OUTPUT_DIR "shadow.vert.spirv");
	const vk::PipelineShaderStageCreateInfo shaderStage{
		.stage = vk::ShaderStageFlagBits::eVertex,
		.module = *vertexShader,
		.pName = "main"
	};
	const vk::GraphicsPipelineCreateInfo pipelineInfo{
		.stageCount = 1,
		.pStages = &shaderStage,
		.pVertexInputState = &vertexInput,
		.pInputAssemblyState = &inputAssembly,
		.pViewportState = &viewport,
		.pRasterizationState = &raster,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depthStencil,
		.pColorBlendState = &blend,
		.pDynamicState = &dynamicState,
		.layout = _layout,
		.renderPass = *_renderPass,
		.subpass = 0
	};
	_pipeline = App::device.createGraphicsPipeline(nullptr, pipelineInfo);
}

}
