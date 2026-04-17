#include "Renderer.hpp"
#include "Renderer.hpp"
#include "Renderer.hpp"
#include "Renderer.hpp"

#include <fstream>

#include "../App.hpp"

namespace vulkan {

uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
	vk::PhysicalDeviceMemoryProperties memProperties;
	App::physicalDevice.getMemoryProperties(&memProperties);
	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) &&
			(memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	throw std::runtime_error("failed to find suitable memory type!");
}

static vk::Format findSupportedFormat(
	const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) {
	for (vk::Format format : candidates) {
		vk::FormatProperties props;
		App::physicalDevice.getFormatProperties(format, &props);

		if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) {
			return format;
		}
		else if (
			tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) {
			return format;
		}
	}
	throw std::runtime_error("failed to find supported format!");
}

static void createImageWithInfo(
	const vk::ImageCreateInfo& imageInfo,
	vk::MemoryPropertyFlags properties,
	vk::Image& image,
	vk::DeviceMemory& imageMemory) {
	if (App::device.createImage(&imageInfo, nullptr, &image) != vk::Result::eSuccess) {
		throw std::runtime_error("failed to create image!");
	}

	vk::MemoryRequirements memRequirements;
	App::device.getImageMemoryRequirements(image, &memRequirements);

	vk::MemoryAllocateInfo allocInfo{};
	allocInfo.sType = vk::StructureType::eMemoryAllocateInfo;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

	if (App::device.allocateMemory(&allocInfo, nullptr, &imageMemory) != vk::Result::eSuccess) {
		throw std::runtime_error("failed to allocate image memory!");
	}

	App::device.bindImageMemory(image, imageMemory, 0);
}

static void vkCheck(vk::Result err) {
	if (err == vk::Result::eSuccess)
		return;
	fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
	if (err < vk::Result::eSuccess)
		abort();
}

Renderer::Renderer() {
	createDepthResources();
	createRenderPass();
	createDescriptorLayout();
	createPipeline();
	createUniformBuffers();
	createDescriptorPool();
	createDescriptorSet();
	createFramebuffers();

	_vectorArrowID = ModelCache::loadModel(ASSET_DIR "other/vectorArrow.obj", Model3D::CreationTransform{ .position = { 0, 0, 0 }, .scale = { 1, 1, 1 }, .rotation = glm::quat{ 1, 0, 0, 0 }, .color = { 1, 1, 1, 1 } });
}

Renderer::~Renderer() {
	ModelCache::unloadModel(_vectorArrowID);
	for (int i = 0; i < 2; i++) {
		App::device.destroyImageView(_depthImageViews[i]);
		App::device.destroyImage(_depthImages[i]);
		App::device.destroyFramebuffer(_frameBuffers[i]);
		App::device.freeMemory(_depthImageMemory[i]);
	}
	App::device.destroyDescriptorPool(_descriptorPool);
	App::device.destroyPipeline(_pipeline);
	App::device.destroyPipelineLayout(_layout);
	App::device.destroyDescriptorSetLayout(_descriptorSetLayout);
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

struct PushConstantData {
	glm::mat4 modelMatrix{ 1.f };
};

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

	PushConstantData push{};

	for (auto& obj : GameObjectContainer::getObjects()) {
		push.modelMatrix = obj.getTransformMatrix();
		_activeCommandBuffer.pushConstants(_layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushConstantData), &push);
		Model3D& model = ModelCache::getModel(obj.model);
		model.bind(_activeCommandBuffer);
		model.draw(_activeCommandBuffer);
	}

	for (auto& arrow : ::App::renderVectors) {
		push.modelMatrix = glm::mat4(1.f);
		push.modelMatrix = glm::translate(push.modelMatrix, arrow.position);
		push.modelMatrix *= glm::toMat4(glm::rotation({ 0, 1, 0 }, glm::normalize(arrow.dir)));
		push.modelMatrix = glm::scale(push.modelMatrix, glm::vec3(::App::vectorScale.x, glm::length(arrow.dir) * ::App::vectorScale.y, ::App::vectorScale.x));
		_activeCommandBuffer.pushConstants(_layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushConstantData), &push);
		Model3D& model = ModelCache::getModel(_vectorArrowID);
		model.bind(_activeCommandBuffer);
		model.draw(_activeCommandBuffer);
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
	vk::PushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(PushConstantData);

	vk::PipelineLayoutCreateInfo layout_info;

	layout_info.pushConstantRangeCount = 1;
	layout_info.pPushConstantRanges = &pushConstantRange;
	layout_info.setLayoutCount = 1;
	layout_info.pSetLayouts = &_descriptorSetLayout;

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
	vk::DescriptorSetLayoutBinding uboLayoutBinding{};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

	vk::DescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &uboLayoutBinding;

	_descriptorSetLayout = App::device.createDescriptorSetLayout(layoutInfo);
}

void Renderer::createDescriptorPool() {
	vk::DescriptorPoolSize poolSize{};
	poolSize.type = vk::DescriptorType::eUniformBuffer;
	poolSize.descriptorCount = static_cast<uint32_t>(2);

	vk::DescriptorPoolCreateInfo poolInfo{};
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = static_cast<uint32_t>(2);

	_descriptorPool = App::device.createDescriptorPool(poolInfo);
}

void Renderer::createDescriptorSet() {
	std::array<vk::DescriptorSetLayout, 2> layouts;
	layouts.fill(_descriptorSetLayout);

	vk::DescriptorSetAllocateInfo allocInfo{};
	allocInfo.descriptorPool = _descriptorPool;
	allocInfo.descriptorSetCount = static_cast<uint32_t>(2);
	allocInfo.pSetLayouts = layouts.data();

	std::vector<vk::DescriptorSet> descriptorSetVec = App::device.allocateDescriptorSets(allocInfo);
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

		_uniformBuffers[i]->map(sizeof(UniformBufferObject));
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