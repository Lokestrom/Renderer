#include "Renderer.hpp"

#include "helpers.hpp"
#include <fstream>
#include <iostream>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>


#include "../App.hpp"

namespace vulkan {

Renderer::Renderer() {
	createDepthResources();
	createRenderPass();
	createDescriptorLayout();
	createPipeline();
	createUniformBuffers();
	createDescriptorPool();
	createDescriptorSet();
	createFramebuffers();

	_vectorArrowID = ModelCache::loadModel(ASSET_DIR "other/vectorArrow.obj");
	_pointID = ModelCache::loadModel(ASSET_DIR "other/point.obj");
	TextureCache::loadDefault(*this);
}

Renderer::~Renderer() {
	ModelCache::unloadModel(_vectorArrowID);
	ModelCache::unloadModel(_pointID);
	TextureCache::unloadDefault();
	for (int i = 0; i < 2; i++) {
		App::device.destroyImageView(_depthImageViews[i]);
		App::device.destroyImage(_depthImages[i]);
		App::device.destroyFramebuffer(_frameBuffers[i]);
		App::device.freeMemory(_depthImageMemory[i]);
		App::device.freeDescriptorSets(_descriptorPool, _descriptorSets[i]);
	}
	App::device.destroyDescriptorPool(_descriptorPool);
	App::device.destroyPipeline(_pipeline);
	App::device.destroyPipelineLayout(_layout);
	App::device.destroyDescriptorSetLayout(_uboDescriptorSetLayout);
	App::device.destroyDescriptorSetLayout(_textureDescriptorSetLayout);
	App::device.destroyRenderPass(_renderPass);
}

void Renderer::setActiveCommandBuffer(vk::CommandBuffer cmd) {
	_activeCommandBuffer = cmd;
}

void Renderer::recreate() {
	App::device.waitIdle();
	for (int i = 0; i < 2; i++) {
		App::device.destroyImageView(_depthImageViews[i]);
		App::device.destroyImage(_depthImages[i]);
		App::device.destroyFramebuffer(_frameBuffers[i]);
		App::device.freeMemory(_depthImageMemory[i]);
	}
	createDepthResources();
	createFramebuffers();
}

void Renderer::render(UniformBufferObject& ubo, uint32_t frameIndex) {
	std::array<vk::ClearValue, 2> clearValues{};

	clearValues[0].color.float32[0] = 0.2;
	clearValues[0].color.float32[1] = 0.2;
	clearValues[0].color.float32[2] = 0.2;
	clearValues[0].color.float32[3] = 1;

	clearValues[1].depthStencil = vk::ClearDepthStencilValue(1.0, 0);

	vk::RenderPassBeginInfo info;
	info.renderPass = _renderPass;
	info.framebuffer = _frameBuffers[frameIndex];
	info.renderArea.extent = { .width = (unsigned int)App::mainWindowData.Width, .height = (unsigned int)App::mainWindowData.Height };
	info.clearValueCount = clearValues.size();
	info.pClearValues = clearValues.data();

	_activeCommandBuffer.beginRenderPass(info, vk::SubpassContents::eInline);

	_activeCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline);

	vk::Viewport vp{
		.width = static_cast<float>(::App::width),
		.height = static_cast<float>(::App::height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};

	_activeCommandBuffer.setViewport(0, vp);

	vk::Rect2D scissor{
		.extent = {
			.width = (uint32_t)::App::width,
			.height = (uint32_t)::App::height }
	};

	_activeCommandBuffer.setScissor(0, scissor);

	_activeCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, _layout, 0, _descriptorSets[App::mainWindowData.FrameIndex], {});

	_uniformBuffers[App::mainWindowData.FrameIndex]->writeToBuffer(&ubo, sizeof(UniformBufferObject));

	VertexPushConstant vertexPush{};
	for (auto& id : GameObjectContainer::getDynamicGameObjects()) {
		auto& obj = GameObjectContainer::get(id);
		if (glm::length((obj.position - glm::vec3(ubo.cameraPos))) > 600) {
			continue;
		}
		vertexPush.modelMatrix = obj.getTransformMatrix();

		_activeCommandBuffer.pushConstants(_layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(VertexPushConstant), &vertexPush);
		Model3D& model = ModelCache::getModel(obj.getModel());
		model.draw(_activeCommandBuffer, _layout);
	}

	const std::array<std::vector<vulkan::ID>*, 9> staticObjects = 
		GameObjectContainer::getStaticGameObjects(glm::vec2{ ubo.cameraPos.x, ubo.cameraPos.z });
	for (auto vector : staticObjects) {
		if (vector == nullptr)
			break;
		for (auto& id : *vector) {
			auto& obj = GameObjectContainer::get(id);
			vertexPush.modelMatrix = obj.getTransformMatrix();

			_activeCommandBuffer.pushConstants(_layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(VertexPushConstant), &vertexPush);
			Model3D& model = ModelCache::getModel(obj.getModel());
			model.draw(_activeCommandBuffer, _layout);
		}
	}

	for (auto& arrow : ::App::renderVectors) {
		if (glm::length2(arrow.dir) == 0) {
			continue;
		}
		vertexPush.modelMatrix = glm::mat4(1.f);
		vertexPush.modelMatrix = glm::translate(vertexPush.modelMatrix, arrow.position);
		vertexPush.modelMatrix *= glm::toMat4(glm::rotation({ 0, 1, 0 }, glm::normalize(arrow.dir)));
		vertexPush.modelMatrix = glm::scale(vertexPush.modelMatrix, glm::vec3(::App::vectorScale.x, glm::length(arrow.dir) * ::App::vectorScale.y, ::App::vectorScale.x));
		_activeCommandBuffer.pushConstants(_layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(VertexPushConstant), &vertexPush);
		Model3D& model = ModelCache::getModel(_vectorArrowID);
		model.draw(_activeCommandBuffer, _layout);
	}

	for (auto& point : ::App::renderPoints) {
		vertexPush.modelMatrix = glm::mat4(1.f);
		vertexPush.modelMatrix = glm::translate(vertexPush.modelMatrix, point.position);
		_activeCommandBuffer.pushConstants(_layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(VertexPushConstant), &vertexPush);
		Model3D& model = ModelCache::getModel(_pointID);
		model.draw(_activeCommandBuffer, _layout);
	}

	_activeCommandBuffer.endRenderPass();
}

static vk::ShaderModule loadShaderModule(const std::string& path) {
	std::ifstream stream(path, std::ios::binary);
	if (!stream) {
		throw std::runtime_error(std::string("Could not open file: ") + path);
	}

	stream.seekg(0, std::ios_base::end);
	std::streampos size = stream.tellg();
	stream.seekg(0, std::ios_base::beg);

	std::vector<char> buffer(size);
	if (!stream.read(buffer.data(), size)) {
		throw std::runtime_error(std::string("Could not read file: ") + path);
	}

	stream.close();

	vk::ShaderModuleCreateInfo shaderModuleCI;
	shaderModuleCI.pCode = (uint32_t*)buffer.data();
	shaderModuleCI.codeSize = buffer.size();

	vk::ShaderModule result = nullptr;
	vkCheck(App::device.createShaderModule(&shaderModuleCI, nullptr, &result));
	return result;
}

void Renderer::createPipeline() {
	vk::PushConstantRange vertexPushConstantRange{};
	vertexPushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
	vertexPushConstantRange.offset = 0;
	vertexPushConstantRange.size = sizeof(VertexPushConstant);

	vk::PushConstantRange fragmentPushConstantRange{};
	fragmentPushConstantRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
	fragmentPushConstantRange.offset = sizeof(VertexPushConstant);
	fragmentPushConstantRange.size = sizeof(FragmentPushConstant);

	std::array<vk::PushConstantRange, 2> pushConstantRanges = { vertexPushConstantRange, fragmentPushConstantRange };

	vk::PipelineLayoutCreateInfo layout_info;

	std::array<vk::DescriptorSetLayout, 2> descriptorSetLayouts = { _uboDescriptorSetLayout, _textureDescriptorSetLayout };

	layout_info.pushConstantRangeCount = pushConstantRanges.size();
	layout_info.pPushConstantRanges = pushConstantRanges.data();
	layout_info.setLayoutCount = descriptorSetLayouts.size();
	layout_info.pSetLayouts = descriptorSetLayouts.data();

	vkCheck(vulkan::App::device.createPipelineLayout(&layout_info, nullptr, &_layout));

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

	vk::StencilOpState front {
		.compareMask = 1,
		.writeMask = 1,
		.reference = 1,
	};

	vk::PipelineDepthStencilStateCreateInfo depth_stencil {
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

	std::string shader_folder{ "" };
	shader_folder = "glsl";

	std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages = { { { .stage = vk::ShaderStageFlagBits::eVertex,
																			 .module = loadShaderModule(SHADER_OUTPUT_DIR "basic.vert.spirv"),
																			 .pName = "main" },
		{ .stage = vk::ShaderStageFlagBits::eFragment,
			.module = loadShaderModule(SHADER_OUTPUT_DIR "basic.frag.spirv"),
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
		.layout = _layout,
		.renderPass = _renderPass,
		.subpass = 0,
	};

	auto result = App::device.createGraphicsPipeline(VK_NULL_HANDLE, pipe, nullptr);
	vkCheck(result.result);

	_pipeline = result.value;

	App::device.destroyShaderModule(shader_stages[0].module);
	App::device.destroyShaderModule(shader_stages[1].module);
}

void Renderer::createDescriptorLayout() {
	vk::DescriptorSetLayoutBinding layoutBinding{
		.binding = 0,
		.descriptorType = vk::DescriptorType::eUniformBuffer,
		.descriptorCount = 1,
		.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
	};

	vk::DescriptorSetLayoutCreateInfo layoutInfo {
		.bindingCount = 1,
		.pBindings = &layoutBinding,
	};

	_uboDescriptorSetLayout = App::device.createDescriptorSetLayout(layoutInfo);

	layoutBinding = {
		.binding = 0,
		.descriptorType = vk::DescriptorType::eCombinedImageSampler,
		.descriptorCount = 1,
		.stageFlags = vk::ShaderStageFlagBits::eFragment,
	};
	layoutInfo = {
		.bindingCount = 1,
		.pBindings = &layoutBinding,
	};

	_textureDescriptorSetLayout = App::device.createDescriptorSetLayout(layoutInfo);
}

void Renderer::createDescriptorPool() {
	vk::DescriptorPoolSize uboPoolSize {
		.type = vk::DescriptorType::eUniformBuffer,
		.descriptorCount = 2,
	};

	vk::DescriptorPoolSize texturePoolSize{
		.type = vk::DescriptorType::eCombinedImageSampler,
		.descriptorCount = 1000,
	};

	std::array<vk::DescriptorPoolSize, 2> poolSizes = { uboPoolSize, texturePoolSize };

	vk::DescriptorPoolCreateInfo poolInfo {
		.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		.maxSets = 1002,
		.poolSizeCount = poolSizes.size(),
		.pPoolSizes = poolSizes.data(),
	};

	_descriptorPool = App::device.createDescriptorPool(poolInfo);
}

void Renderer::createDescriptorSet() {
	std::array<vk::DescriptorSetLayout, 2> layouts;
	layouts.fill(_uboDescriptorSetLayout);

	vk::DescriptorSetAllocateInfo allocInfo{};
	allocInfo.descriptorPool = _descriptorPool;
	allocInfo.descriptorSetCount = layouts.size();
	allocInfo.pSetLayouts = layouts.data();

	std::vector<vk::DescriptorSet> descriptorSetVec = App::device.allocateDescriptorSets(allocInfo);
	assert(descriptorSetVec.size() == 2 && "Incorrect number of allocated descriptor sets");
	std::move(descriptorSetVec.begin(), descriptorSetVec.end(), _descriptorSets.data());

	for (size_t i = 0; i < 2; i++) {
		vk::DescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = _uniformBuffers[i]->getBuffer();
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(UniformBufferObject);

		vk::WriteDescriptorSet descriptorWrite{};
		descriptorWrite.dstSet = _descriptorSets[i];
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo = &bufferInfo;

		App::device.updateDescriptorSets(descriptorWrite, {});
	}
}

void Renderer::createRenderPass() {
	std::array<vk::AttachmentDescription, 2> attachments = {};
	
	attachments[0].format = vk::Format::eB8G8R8A8Unorm;
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
		_uniformBuffers[i] = std::make_unique<Buffer>(
			sizeof(UniformBufferObject),
			1,
			vk::BufferUsageFlagBits::eUniformBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		if (_uniformBuffers[i]->map(sizeof(UniformBufferObject)) != vk::Result::eSuccess)
			throw std::runtime_error("Failed to map buffer when creating uniform buffers");
	}
}

void Renderer::createDepthResources() {
	_depthFormat = findSupportedFormat(
		{ vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
		vk::ImageTiling::eOptimal,
		vk::FormatFeatureFlagBits::eDepthStencilAttachment);

	vk::Extent2D swapChainExtent = { .width = (uint32_t)App::mainWindowData.Width, .height = (uint32_t)App::mainWindowData.Height };

	for (int i = 0; i < _depthImages.size(); i++) {
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
		viewInfo.image = _depthImages[i];
		viewInfo.viewType = vk::ImageViewType::e2D;
		viewInfo.format = _depthFormat;
		viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		if (App::device.createImageView(&viewInfo, nullptr, &_depthImageViews[i]) != vk::Result::eSuccess) {
			throw std::runtime_error("failed to create texture image view!");
		}
	}
}

void Renderer::createFramebuffers() {
	for (size_t i = 0; i < _depthImages.size(); i++) {
		std::array<vk::ImageView, 2> attachments = { *reinterpret_cast<vk::ImageView*>(&App::mainWindowData.Frames[i].BackbufferView), _depthImageViews[i] };

		vk::Extent2D swapChainExtent = { .width = (uint32_t)::App::width, .height = (uint32_t)::App::height };
		vk::FramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.renderPass = _renderPass;
		framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		framebufferInfo.pAttachments = attachments.data();
		framebufferInfo.width = swapChainExtent.width;
		framebufferInfo.height = swapChainExtent.height;
		framebufferInfo.layers = 1;

		if (App::device.createFramebuffer(&framebufferInfo, nullptr, &_frameBuffers[i]) != vk::Result::eSuccess) {
			throw std::runtime_error("failed to create framebuffer!");
		}
	}
}


}