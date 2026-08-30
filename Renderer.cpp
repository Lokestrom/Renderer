#include "Renderer.hpp"

#include "helpers.hpp"
#include "gameObject.hpp"
#include "Runtime.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace renderer {

Renderer::Renderer() {
	validateBindlessTextureLimits();
	createDepthResources();
	createRenderPass();
	createDescriptorLayout();
	createPipeline();
	_shadowRenderer.initialize(*_layout);
	createUniformBuffers();
	createDescriptorPool();
	createDescriptorSet();
	createFramebuffers();

	TextureCache::loadDefault();
}

Renderer::~Renderer() {
	TextureCache::unloadDefault();
}

void Renderer::setActiveCommandBuffer(vk::CommandBuffer cmd) noexcept {
	assert(cmd && "Active command buffer must be valid");
	_activeCommandBuffer = cmd;
}

void Renderer::releaseSwapChainResources() noexcept {
	_frameBuffers.clear();
	_depthImageViews.clear();
	_depthImages.clear();
	_depthImageMemory.clear();
}

void Renderer::recreateSwapChainResources(bool formatChanged) {
	if (formatChanged || !*_pipeline || !*_renderPass) {
		_pipeline.clear();
		_renderPass.clear();
		createRenderPass();
		createPipeline(false);
	}
	createDepthResources();
	createFramebuffers();
}

void Renderer::render(
	const UniformBufferObject& ubo,
	const uint32_t frameIndex,
	const uint32_t imageIndex,
	bool drawScene) noexcept {
	assert(frameIndex < _uboDescriptorSets.size() && "Frame index is outside the descriptor set array");
	assert(imageIndex < _frameBuffers.size() && "Swap-chain image index is outside the framebuffer array");
	assert(_activeCommandBuffer && "Renderer must have an active command buffer before rendering");
	assert(*_textureDescriptorSets[frameIndex] && "Texture descriptor set must be allocated before rendering");
	const RendererSettings& settings = Runtime::configuration().renderer;

	if (drawScene) [[likely]] {
		try {
			_textureStreamer.update(ubo, frameIndex, settings.dynamicObjectViewDistance);
		}
		catch (const std::exception& e) {
			Runtime::log(LogLevel::warning, std::string("Texture streaming update failed: ") + e.what());
		}
	}

	if (drawScene) [[likely]] {
		UniformBufferObject renderUbo = ubo;
		_shadowRenderer.updateCascadeUniforms(renderUbo);
		_uniformBuffers[frameIndex].writeToBuffer(&renderUbo, sizeof(renderUbo));

		_shadowRenderer.render(
			_activeCommandBuffer, renderUbo, frameIndex, *_uboDescriptorSets[frameIndex]);
	}

	std::array<vk::ClearValue, 2> clearValues{};

	clearValues[0].color.float32[0] = settings.backgroundColor.r;
	clearValues[0].color.float32[1] = settings.backgroundColor.g;
	clearValues[0].color.float32[2] = settings.backgroundColor.b;
	clearValues[0].color.float32[3] = 1;

	clearValues[1].depthStencil = vk::ClearDepthStencilValue(1.0, 0);

	vk::RenderPassBeginInfo info{
		.renderPass = *_renderPass,
		.framebuffer = *_frameBuffers[imageIndex],
		.renderArea = { .extent = App::extent() },
		.clearValueCount = clearValues.size(),
		.pClearValues = clearValues.data()
	};

	_activeCommandBuffer.beginRenderPass(info, vk::SubpassContents::eInline);

	if (!drawScene) [[unlikely]] {
		_activeCommandBuffer.endRenderPass();
		return;
	}

	_activeCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *_pipeline);
	const vk::Extent2D extent = App::extent();

	vk::Viewport vp{
		.width = static_cast<float>(extent.width),
		.height = static_cast<float>(extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};

	_activeCommandBuffer.setViewport(0, vp);

	vk::Rect2D scissor{
		.extent = extent
	};

	_activeCommandBuffer.setScissor(0, scissor);

	const std::array<vk::DescriptorSet, 2> descriptorSets = {
		*_uboDescriptorSets[frameIndex],
		*_textureDescriptorSets[frameIndex]
	};
	_activeCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_layout, 0, descriptorSets, {});

	VertexPushConstant vertexPush{};
	for (auto& id : GameObjectContainer::getDynamicGameObjects()) {
		auto& obj = GameObjectContainer::get(id);
		if (glm::length2((obj.position - glm::vec3(ubo.cameraPos))) > settings.dynamicObjectViewDistance * settings.dynamicObjectViewDistance) {
			continue;
		}
		vertexPush.modelMatrix = obj.getTransformMatrix();

		_activeCommandBuffer.pushConstants(*_layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(VertexPushConstant), &vertexPush);
		Model3D& model = ModelCache::getModel(obj.getModel());
		model.draw(_activeCommandBuffer, *_layout);
	}

	const std::array<GameObjectContainer::StaticChunk, 9> staticChunks =
		GameObjectContainer::getStaticGameObjectChunks(glm::vec2{ ubo.cameraPos.x, ubo.cameraPos.z });
	for (const auto& chunk : staticChunks) {
		if (chunk.objects == nullptr)
			break;
		for (auto& id : *chunk.objects) {
			auto& obj = GameObjectContainer::get(id);
			vertexPush.modelMatrix = obj.getTransformMatrix();

			_activeCommandBuffer.pushConstants(*_layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(VertexPushConstant), &vertexPush);
			Model3D& model = ModelCache::getModel(obj.getModel());
			model.draw(_activeCommandBuffer, *_layout);
		}
	}

	_activeCommandBuffer.endRenderPass();
}

void Renderer::validateBindlessTextureLimits() const {
	const vk::PhysicalDeviceLimits& limits = App::physicalDevice.getProperties().limits;
	constexpr uint32_t requiredSampledImages = MaxBindlessTextures + 1;
	constexpr uint32_t requiredFragmentResources = MaxBindlessTextures + 2;

	const bool supported =
		requiredSampledImages <= limits.maxPerStageDescriptorSamplers &&
		requiredSampledImages <= limits.maxPerStageDescriptorSampledImages &&
		requiredFragmentResources <= limits.maxPerStageResources &&
		requiredSampledImages <= limits.maxDescriptorSetSamplers &&
		requiredSampledImages <= limits.maxDescriptorSetSampledImages &&
		ShadowRenderer::Resolution <= limits.maxImageDimension2D;

	if (!supported) {
		throw std::runtime_error(
			"GPU does not support the configured bindless texture descriptor count of " +
			std::to_string(MaxBindlessTextures) + " plus the shadow map");
	}
}

void Renderer::createPipeline(bool createLayout) {
	if (createLayout) {
		const vk::PushConstantRange vertexPushConstantRange{
			.stageFlags = vk::ShaderStageFlagBits::eVertex,
			.offset = 0,
			.size = sizeof(VertexPushConstant)
		};
		const vk::PushConstantRange fragmentPushConstantRange{
			.stageFlags = vk::ShaderStageFlagBits::eFragment,
			.offset = sizeof(VertexPushConstant),
			.size = sizeof(FragmentPushConstant)
		};
		const std::array<vk::PushConstantRange, 2> pushConstantRanges{
			vertexPushConstantRange,
			fragmentPushConstantRange
		};
		const std::array<vk::DescriptorSetLayout, 2> descriptorSetLayouts{
			*_uboDescriptorSetLayout,
			*_textureDescriptorSetLayout
		};
		vk::PipelineLayoutCreateInfo layoutInfo {
			.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size()),
			.pSetLayouts = descriptorSetLayouts.data(),
			.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size()),
			.pPushConstantRanges = pushConstantRanges.data()
		};
		_layout = App::device.createPipelineLayout(layoutInfo);
	}

	auto bindingDescription = Model3D::Vertex::bindingDescriptions();

	auto attributeDescriptions = Model3D::Vertex::attributeDescriptions();

	vk::PipelineVertexInputStateCreateInfo vertex_input{
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = bindingDescription.data(),
		.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
		.pVertexAttributeDescriptions = attributeDescriptions.data()
	};

	vk::PipelineInputAssemblyStateCreateInfo input_assembly{
		.topology = vk::PrimitiveTopology::eTriangleList,
		.primitiveRestartEnable = VK_FALSE
	};

	vk::PipelineRasterizationStateCreateInfo raster{
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = vk::PolygonMode::eFill,
		.cullMode = vk::CullModeFlagBits::eBack,
		.frontFace = vk::FrontFace::eCounterClockwise,
		.depthBiasEnable = VK_FALSE,
		.lineWidth = 1.0f,
	};

	std::vector<vk::DynamicState> dynamic_states = {
		vk::DynamicState::eViewport,
		vk::DynamicState::eScissor,
	};

	vk::PipelineColorBlendAttachmentState blend_attachment{
		.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
	};


	vk::PipelineColorBlendStateCreateInfo blend{
		.attachmentCount = 1,
		.pAttachments = &blend_attachment
	};

	vk::PipelineViewportStateCreateInfo viewport{
		.viewportCount = 1,
		.scissorCount = 1
	};

	vk::StencilOpState front{
		.compareMask = 1,
		.writeMask = 1,
		.reference = 1,
	};

	vk::PipelineDepthStencilStateCreateInfo depth_stencil{
		.depthTestEnable = true,
		.depthWriteEnable = true,
		.depthCompareOp = vk::CompareOp::eLess,
		.front = front,
	};

	vk::PipelineMultisampleStateCreateInfo multisample{
		.rasterizationSamples = vk::SampleCountFlagBits::e1
	};

	vk::PipelineDynamicStateCreateInfo dynamic_state_info{
		.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
		.pDynamicStates = dynamic_states.data()
	};

	vk::raii::ShaderModule vertexShader = loadShaderModule(
		RENDERING_ENGINE_SHADER_DIRECTORY "/basic.vert.spirv");
	vk::raii::ShaderModule fragmentShader = loadShaderModule(
		RENDERING_ENGINE_SHADER_DIRECTORY "/basic.frag.spirv");
	std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages = { { { .stage = vk::ShaderStageFlagBits::eVertex,
																			 .module = *vertexShader,
																			 .pName = "main" },
		{ .stage = vk::ShaderStageFlagBits::eFragment,
			.module = *fragmentShader,
			.pName = "main" } } };

	vk::GraphicsPipelineCreateInfo pipe{
		.stageCount = shader_stages.size(),
		.pStages = shader_stages.data(),
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport,
		.pRasterizationState = &raster,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth_stencil,
		.pColorBlendState = &blend,
		.pDynamicState = &dynamic_state_info,
		.layout = *_layout,
		.renderPass = *_renderPass,
		.subpass = 0,
	};

	_pipeline = App::device.createGraphicsPipeline(nullptr, pipe);
}

void Renderer::createDescriptorLayout() {
	const std::array uboLayoutBindings = {
		vk::DescriptorSetLayoutBinding{
			.binding = 0,
			.descriptorType = vk::DescriptorType::eUniformBuffer,
			.descriptorCount = 1,
			.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
		},
		vk::DescriptorSetLayoutBinding{
			.binding = 1,
			.descriptorType = vk::DescriptorType::eCombinedImageSampler,
			.descriptorCount = 1,
			.stageFlags = vk::ShaderStageFlagBits::eFragment,
		}
	};

	vk::DescriptorSetLayoutCreateInfo layoutInfo{
		.bindingCount = static_cast<uint32_t>(uboLayoutBindings.size()),
		.pBindings = uboLayoutBindings.data(),
	};

	_uboDescriptorSetLayout = App::device.createDescriptorSetLayout(layoutInfo);

	vk::DescriptorSetLayoutBinding layoutBinding{
		.binding = 0,
		.descriptorType = vk::DescriptorType::eCombinedImageSampler,
		.descriptorCount = MaxBindlessTextures,
		.stageFlags = vk::ShaderStageFlagBits::eFragment,
	};
	layoutInfo = {
		.bindingCount = 1,
		.pBindings = &layoutBinding,
	};

	_textureDescriptorSetLayout = App::device.createDescriptorSetLayout(layoutInfo);
}

void Renderer::createDescriptorPool() {
	vk::DescriptorPoolSize uboPoolSize{
		.type = vk::DescriptorType::eUniformBuffer,
		.descriptorCount = 2,
	};

	vk::DescriptorPoolSize texturePoolSize{
		.type = vk::DescriptorType::eCombinedImageSampler,
		.descriptorCount = static_cast<uint32_t>(
			MaxBindlessTextures * _textureDescriptorSets.size() + ShadowRenderer::FrameCount),
	};

	std::array<vk::DescriptorPoolSize, 2> poolSizes = { uboPoolSize, texturePoolSize };

	vk::DescriptorPoolCreateInfo poolInfo{
		.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		.maxSets = static_cast<uint32_t>(_uboDescriptorSets.size() + _textureDescriptorSets.size()),
		.poolSizeCount = poolSizes.size(),
		.pPoolSizes = poolSizes.data(),
	};

	_descriptorPool = App::device.createDescriptorPool(poolInfo);
}

void Renderer::createDescriptorSet() {
	const std::array<vk::DescriptorSetLayout, 2> layouts{ *_uboDescriptorSetLayout, *_uboDescriptorSetLayout };
	const vk::DescriptorSetAllocateInfo allocInfo{
		.descriptorPool = *_descriptorPool,
		.descriptorSetCount = static_cast<uint32_t>(layouts.size()),
		.pSetLayouts = layouts.data()
	};

	std::vector<vk::raii::DescriptorSet> descriptorSetVec = App::device.allocateDescriptorSets(allocInfo);
	assert(descriptorSetVec.size() == 2 && "Incorrect number of allocated descriptor sets");
	std::move(descriptorSetVec.begin(), descriptorSetVec.end(), _uboDescriptorSets.data());

	for (size_t i = 0; i < 2; i++) {
		const vk::DescriptorBufferInfo bufferInfo{
			.buffer = _uniformBuffers[i].getBuffer(),
			.offset = 0,
			.range = sizeof(UniformBufferObject)
		};

		const vk::DescriptorImageInfo shadowInfo = _shadowRenderer.getDescriptorImageInfo(static_cast<uint32_t>(i));
		const std::array descriptorWrites = {
			vk::WriteDescriptorSet{
				.dstSet = *_uboDescriptorSets[i],
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eUniformBuffer,
				.pBufferInfo = &bufferInfo
			},
			vk::WriteDescriptorSet{
				.dstSet = *_uboDescriptorSets[i],
				.dstBinding = 1,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.pImageInfo = &shadowInfo
			}
		};

		App::device.updateDescriptorSets(descriptorWrites, {});
	}

	const std::array<vk::DescriptorSetLayout, 2> textureLayouts{ *_textureDescriptorSetLayout, *_textureDescriptorSetLayout };
	const vk::DescriptorSetAllocateInfo textureAllocInfo{
		.descriptorPool = *_descriptorPool,
		.descriptorSetCount = static_cast<uint32_t>(textureLayouts.size()),
		.pSetLayouts = textureLayouts.data()
	};

	std::vector<vk::raii::DescriptorSet> textureDescriptorSetVec = App::device.allocateDescriptorSets(textureAllocInfo);
	assert(textureDescriptorSetVec.size() == _textureDescriptorSets.size() && "Incorrect number of allocated texture descriptor sets");
	std::move(textureDescriptorSetVec.begin(), textureDescriptorSetVec.end(), _textureDescriptorSets.data());

	const std::array<vk::DescriptorSet, 2> textureDescriptorSets{
		*_textureDescriptorSets[0],
		*_textureDescriptorSets[1]
	};
	TextureCache::initializeBindlessDescriptorSets(textureDescriptorSets);
}

void Renderer::createRenderPass() {
	std::array<vk::AttachmentDescription, 2> attachments = {};

	attachments[0].format = App::surfaceFormat();
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

	attachments[1].format = _depthFormat;
	attachments[1].samples = vk::SampleCountFlagBits::e1;
	attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[1].storeOp = vk::AttachmentStoreOp::eDontCare;
	attachments[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[1].initialLayout = vk::ImageLayout::eUndefined;
	attachments[1].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

	vk::AttachmentReference colorRef{};
	colorRef.attachment = 0;
	colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

	vk::AttachmentReference depthRef{};
	depthRef.attachment = 1;
	depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

	vk::SubpassDescription subpass{};
	subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;
	subpass.pDepthStencilAttachment = &depthRef;

	vk::SubpassDependency dependency{};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask =
		vk::PipelineStageFlagBits::eColorAttachmentOutput |
		vk::PipelineStageFlagBits::eEarlyFragmentTests;
	dependency.dstStageMask =
		vk::PipelineStageFlagBits::eColorAttachmentOutput |
		vk::PipelineStageFlagBits::eEarlyFragmentTests;
	dependency.dstAccessMask =
		vk::AccessFlagBits::eColorAttachmentWrite |
		vk::AccessFlagBits::eDepthStencilAttachmentWrite;

	vk::RenderPassCreateInfo info{};
	info.attachmentCount = attachments.size();
	info.pAttachments = attachments.data();
	info.subpassCount = 1;
	info.pSubpasses = &subpass;
	info.dependencyCount = 1;
	info.pDependencies = &dependency;

	_renderPass = App::device.createRenderPass(info);
}

void Renderer::createUniformBuffers() {
	for (size_t i = 0; i < 2; i++) {
		_uniformBuffers[i] = Buffer(
			sizeof(UniformBufferObject),
			1,
			vk::BufferUsageFlagBits::eUniformBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		_uniformBuffers[i].map(sizeof(UniformBufferObject));
	}
}

void Renderer::createDepthResources() {
	_depthFormat = findSupportedFormat(
		{ vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
		vk::ImageTiling::eOptimal,
		vk::FormatFeatureFlagBits::eDepthStencilAttachment);

	const vk::Extent2D swapChainExtent = App::extent();
	const size_t imageCount = App::imageCount();
	_depthImageMemory.reserve(imageCount);
	_depthImages.reserve(imageCount);
	_depthImageViews.reserve(imageCount);
	for (size_t i = 0; i < imageCount; ++i) {
		_depthImageMemory.emplace_back(nullptr);
		_depthImages.emplace_back(nullptr);
		_depthImageViews.emplace_back(nullptr);
	}

	for (size_t i = 0; i < _depthImages.size(); ++i) {
		vk::ImageCreateInfo imageInfo{};
		imageInfo.sType = vk::StructureType::eImageCreateInfo;
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.extent.width = swapChainExtent.width;
		imageInfo.extent.height = swapChainExtent.height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = _depthFormat;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.sharingMode = vk::SharingMode::eExclusive;
		imageInfo.flags = vk::ImageCreateFlagBits();

		createImageWithInfo(
			imageInfo,
			vk::MemoryPropertyFlagBits::eDeviceLocal,
			_depthImages[i],
			_depthImageMemory[i]);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.sType = vk::StructureType::eImageViewCreateInfo;
		viewInfo.image = *_depthImages[i];
		viewInfo.viewType = vk::ImageViewType::e2D;
		viewInfo.format = _depthFormat;
		viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		_depthImageViews[i] = App::device.createImageView(viewInfo);
	}
}

void Renderer::createFramebuffers() {
	const std::span<const vk::ImageView> swapChainViews = App::swapChainImageViews();
	assert(swapChainViews.size() == _depthImages.size());
	_frameBuffers.reserve(swapChainViews.size());
	for (size_t i = 0; i < _depthImages.size(); ++i) {
		std::array<vk::ImageView, 2> attachments = {
			swapChainViews[i],
			*_depthImageViews[i]
		};

		const vk::Extent2D swapChainExtent = App::extent();
		vk::FramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.renderPass = *_renderPass;
		framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		framebufferInfo.pAttachments = attachments.data();
		framebufferInfo.width = swapChainExtent.width;
		framebufferInfo.height = swapChainExtent.height;
		framebufferInfo.layers = 1;

		_frameBuffers.emplace_back(App::device.createFramebuffer(framebufferInfo));
	}
}


}
