#include "VulkanApp.hpp"

#include "Renderer.hpp"
#include "Runtime.hpp"

#include <iostream>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace renderer {

namespace {
std::optional<Renderer> activeRenderer;

[[nodiscard]] Renderer& getRenderer() noexcept {
	assert(activeRenderer.has_value() && "Renderer must be initialized");
	return activeRenderer.value();
}

[[nodiscard]] bool extensionAvailable(const std::vector<vk::ExtensionProperties>& properties, const char* extension) noexcept {
	return std::ranges::any_of(properties, [extension](const vk::ExtensionProperties& property) {
		return std::strcmp(property.extensionName, extension) == 0;
	});
}

[[nodiscard]] vk::SurfaceFormatKHR selectSurfaceFormat(
	const vk::raii::PhysicalDevice& physicalDevice,
	vk::SurfaceKHR surface) {
	const std::vector<vk::SurfaceFormatKHR> formats = physicalDevice.getSurfaceFormatsKHR(surface);
	if (formats.empty()) {
		throw std::runtime_error("Surface exposes no Vulkan formats");
	}

	constexpr std::array preferredFormats{
		vk::Format::eB8G8R8A8Unorm,
		vk::Format::eR8G8B8A8Unorm,
		vk::Format::eB8G8R8Unorm,
		vk::Format::eR8G8B8Unorm
	};
	if (formats.size() == 1 && formats.front().format == vk::Format::eUndefined) {
		return { preferredFormats.front(), vk::ColorSpaceKHR::eSrgbNonlinear };
	}
	for (vk::Format preferred : preferredFormats) {
		for (const vk::SurfaceFormatKHR& available : formats) {
			if (available.format == preferred && available.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
				return available;
			}
		}
	}
	return formats.front();
}

[[nodiscard]] vk::PresentModeKHR selectPresentMode(
	const vk::raii::PhysicalDevice& physicalDevice,
	vk::SurfaceKHR surface) {
	const std::vector<vk::PresentModeKHR> modes = physicalDevice.getSurfacePresentModesKHR(surface);

#ifdef RENDERING_ENGINE_USE_UNLIMITED_FRAME_RATE
	constexpr std::array preferredModes{
		vk::PresentModeKHR::eMailbox,
		vk::PresentModeKHR::eImmediate,
		vk::PresentModeKHR::eFifo
	};
#else
	constexpr std::array preferredModes{ vk::PresentModeKHR::eFifo };
#endif
	for (vk::PresentModeKHR preferred : preferredModes) {
		if (std::ranges::find(modes, preferred) != modes.end()) {
			return preferred;
		}
	}
	return vk::PresentModeKHR::eFifo;
}

#ifdef RENDERING_ENGINE_USE_VULKAN_DEBUG_REPORT
VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugReport(
	VkDebugReportFlagsEXT,
	VkDebugReportObjectTypeEXT objectType,
	uint64_t,
	size_t,
	int32_t,
	const char*,
	const char* message,
	void*) {
	Runtime::log(LogLevel::error, std::string("[renderer Vulkan] Debug report from ObjectType: ") + std::to_string(objectType) + "\nMessage: " + message + "\n\n");
	return VK_FALSE;
}
#endif
}

void App::startup(std::vector<const char*> instanceExtensions) {
	const Configuration& configuration = Runtime::configuration();
	const vk::ApplicationInfo applicationInfo{
		.pApplicationName = configuration.applicationName.c_str(),
		.applicationVersion = configuration.applicationVersion,
		.pEngineName = configuration.engineName.c_str(),
		.engineVersion = configuration.engineVersion,
		.apiVersion = VK_API_VERSION_1_4
	};

	vk::InstanceCreateInfo instanceCreateInfo{};
	instanceCreateInfo.pApplicationInfo = &applicationInfo;
	const std::vector<vk::ExtensionProperties> instanceProperties = context.enumerateInstanceExtensionProperties();
	if (extensionAvailable(instanceProperties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
		instanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
	}
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
	if (extensionAvailable(instanceProperties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
		instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
		instanceCreateInfo.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
	}
#endif

#ifdef RENDERING_ENGINE_USE_VULKAN_DEBUG_REPORT
	constexpr std::array<const char*, 1> layers{ "VK_LAYER_KHRONOS_validation" };
	instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
	instanceCreateInfo.ppEnabledLayerNames = layers.data();
	instanceExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
	instanceExtensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
	std::array enables{
		vk::ValidationFeatureEnableEXT::eGpuAssisted,
		vk::ValidationFeatureEnableEXT::eGpuAssistedReserveBindingSlot,
		vk::ValidationFeatureEnableEXT::eSynchronizationValidation
	};
	vk::ValidationFeaturesEXT validationFeatures{
		.enabledValidationFeatureCount = static_cast<uint32_t>(enables.size()),
		.pEnabledValidationFeatures = enables.data()
	};
	instanceCreateInfo.pNext = &validationFeatures;
#endif
	instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
	instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
	instance = context.createInstance(instanceCreateInfo);

#ifdef RENDERING_ENGINE_USE_VULKAN_DEBUG_REPORT
	vk::DebugReportCallbackCreateInfoEXT callbackInfo{};
	callbackInfo.flags = vk::DebugReportFlagBitsEXT::eError |
		vk::DebugReportFlagBitsEXT::eWarning |
		vk::DebugReportFlagBitsEXT::ePerformanceWarning;
	callbackInfo.pfnCallback = reinterpret_cast<vk::PFN_DebugReportCallbackEXT>(vulkanDebugReport);
	debugReport = instance.createDebugReportCallbackEXT(callbackInfo);
#endif
}

void App::startupWindow(vk::raii::SurfaceKHR surface, uint32_t width, uint32_t height) {
	assert(*surface && "Invalid Vulkan surface");
	swapChain.surface = std::move(surface);

	std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();
	if (physicalDevices.empty()) {
		throw std::runtime_error("No Vulkan physical device is available");
	}
	std::optional<size_t> selectedDeviceIndex;
	uint32_t selectedQueueFamily = UINT32_MAX;
	bool selectedIsDiscrete = false;
	for (size_t deviceIndex = 0; deviceIndex < physicalDevices.size(); ++deviceIndex) {
		vk::raii::PhysicalDevice& candidate = physicalDevices[deviceIndex];
		const std::vector<vk::QueueFamilyProperties> families = candidate.getQueueFamilyProperties();
		for (uint32_t index = 0; index < families.size(); ++index) {
			const bool supportsPresentation = candidate.getSurfaceSupportKHR(index, *swapChain.surface);
			if (!(families[index].queueFlags & vk::QueueFlagBits::eGraphics) || !supportsPresentation) {
				continue;
			}
			const bool isDiscrete = candidate.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
			if (!selectedDeviceIndex || (isDiscrete && !selectedIsDiscrete)) {
				selectedDeviceIndex = deviceIndex;
				selectedQueueFamily = index;
				selectedIsDiscrete = isDiscrete;
			}
			break;
		}
	}
	if (!selectedDeviceIndex) {
		throw std::runtime_error("No Vulkan graphics queue can present to the supplied surface");
	}
	queueFamily = selectedQueueFamily;
	physicalDevice = std::move(physicalDevices[*selectedDeviceIndex]);

	std::vector<const char*> deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	const std::vector<vk::ExtensionProperties> deviceProperties = physicalDevice.enumerateDeviceExtensionProperties();
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
	if (extensionAvailable(deviceProperties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
		deviceExtensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
	}
#endif
	constexpr std::array<float, 1> queuePriority{ 1.0f };
	vk::DeviceQueueCreateInfo queueCreateInfo {
		.queueFamilyIndex = queueFamily,
		.queueCount = static_cast<uint32_t>(queuePriority.size()),
		.pQueuePriorities = queuePriority.data(),
	};
	vk::PhysicalDeviceFeatures2 features{
		.features = vk::PhysicalDeviceFeatures{
			.shaderSampledImageArrayDynamicIndexing = true
		}
	};
	vk::DeviceCreateInfo deviceCreateInfo{
		.pNext = &features,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queueCreateInfo,
		.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
		.ppEnabledExtensionNames = deviceExtensions.data(),
	};
	device = physicalDevice.createDevice(deviceCreateInfo);
	queue = device.getQueue(queueFamily, 0);

	createSwapChain(width, height);
	activeRenderer.emplace();
	lastAppliedSurfaceFormat = swapChain.format.format;
}

void App::createSwapChain(uint32_t width, uint32_t height) {
	swapChain.format = selectSurfaceFormat(physicalDevice, *swapChain.surface);
	swapChain.presentMode = selectPresentMode(physicalDevice, *swapChain.surface);

	const vk::SurfaceCapabilitiesKHR capabilities = physicalDevice.getSurfaceCapabilitiesKHR(*swapChain.surface);
	if (!(capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment)) {
		throw std::runtime_error("Vulkan surface cannot be used as a color attachment");
	}

	if (capabilities.currentExtent.width == std::numeric_limits<uint32_t>::max()) {
		if (width < capabilities.minImageExtent.width || width > capabilities.maxImageExtent.width ||
			height < capabilities.minImageExtent.height || height > capabilities.maxImageExtent.height) [[unlikely]] {
			Runtime::log(LogLevel::warning, "Vulkan surface size is outside of the supported range, clamping to supported range");
		}
		swapChain.extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		swapChain.extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
	}
	else {
		swapChain.extent = capabilities.currentExtent;
	}

	uint32_t requestedImageCount = std::max(FramesInFlight, capabilities.minImageCount);
	if (capabilities.maxImageCount != 0) {
		requestedImageCount = std::min(requestedImageCount, capabilities.maxImageCount);
	}
	vk::CompositeAlphaFlagBitsKHR compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	constexpr std::array compositeCandidates{
		vk::CompositeAlphaFlagBitsKHR::eOpaque,
		vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
		vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
		vk::CompositeAlphaFlagBitsKHR::eInherit
	};
	for (vk::CompositeAlphaFlagBitsKHR candidate : compositeCandidates) {
		if (capabilities.supportedCompositeAlpha & candidate) {
			compositeAlpha = candidate;
			break;
		}
	}
	const vk::SwapchainCreateInfoKHR swapChainInfo{
		.surface = *swapChain.surface,
		.minImageCount = requestedImageCount,
		.imageFormat = swapChain.format.format,
		.imageColorSpace = swapChain.format.colorSpace,
		.imageExtent = swapChain.extent,
		.imageArrayLayers = 1,
		.imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
		.imageSharingMode = vk::SharingMode::eExclusive,
		.preTransform = capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity
			? vk::SurfaceTransformFlagBitsKHR::eIdentity
			: capabilities.currentTransform,
		.compositeAlpha = compositeAlpha,
		.presentMode = swapChain.presentMode,
		.clipped = true
	};
	swapChain.handle = device.createSwapchainKHR(swapChainInfo);
	swapChain.images = swapChain.handle.getImages();
	const size_t imageCount = swapChain.images.size();
	swapChain.ownedImageViews.reserve(imageCount);
	swapChain.imageViews.reserve(imageCount);
	swapChain.imagesInFlight.assign(imageCount, vk::Fence{});
	swapChain.renderCompleteSemaphores.reserve(imageCount);

	for (vk::Image image : swapChain.images) {
		const vk::ImageViewCreateInfo viewInfo{
			.image = image,
			.viewType = vk::ImageViewType::e2D,
			.format = swapChain.format.format,
			.components = {
				vk::ComponentSwizzle::eR,
				vk::ComponentSwizzle::eG,
				vk::ComponentSwizzle::eB,
				vk::ComponentSwizzle::eA
			},
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};
		swapChain.ownedImageViews.emplace_back(device.createImageView(viewInfo));
		swapChain.imageViews.emplace_back(*swapChain.ownedImageViews.back());
		swapChain.renderCompleteSemaphores.emplace_back(device.createSemaphore(vk::SemaphoreCreateInfo{}));
	}

	const vk::AttachmentDescription attachment{
		.format = swapChain.format.format,
		.samples = vk::SampleCountFlagBits::e1,
		.loadOp = vk::AttachmentLoadOp::eLoad,
		.storeOp = vk::AttachmentStoreOp::eStore,
		.stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
		.stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
		.initialLayout = vk::ImageLayout::eColorAttachmentOptimal,
		.finalLayout = vk::ImageLayout::ePresentSrcKHR
	};
	const vk::AttachmentReference colorReference{
		.attachment = 0,
		.layout = vk::ImageLayout::eColorAttachmentOptimal
	};
	const vk::SubpassDescription subpass{
		.pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorReference
	};
	const vk::SubpassDependency dependency{
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
		.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
		.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
		.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite
	};
	const vk::RenderPassCreateInfo passInfo{
		.attachmentCount = 1,
		.pAttachments = &attachment,
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &dependency
	};
	swapChain.presentationPass = device.createRenderPass(passInfo);

	swapChain.presentationFramebuffers.reserve(imageCount);
	for (vk::ImageView imageView : swapChain.imageViews) {
		const vk::FramebufferCreateInfo framebufferInfo{
			.renderPass = *swapChain.presentationPass,
			.attachmentCount = 1,
			.pAttachments = &imageView,
			.width = swapChain.extent.width,
			.height = swapChain.extent.height,
			.layers = 1
		};
		swapChain.presentationFramebuffers.emplace_back(device.createFramebuffer(framebufferInfo));
	}

	for (FrameSlot& frame : swapChain.frames) {
		const vk::CommandPoolCreateInfo poolInfo{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			.queueFamilyIndex = queueFamily
		};
		frame.commandPool = device.createCommandPool(poolInfo);
		const vk::CommandBufferAllocateInfo allocateInfo{
			.commandPool = *frame.commandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = 1
		};
		auto commandBuffers = device.allocateCommandBuffers(allocateInfo);
		frame.commandBuffer = std::move(commandBuffers.front());
		frame.fence = device.createFence(vk::FenceCreateInfo{
			.flags = vk::FenceCreateFlagBits::eSignaled
		});
		frame.imageAcquired = device.createSemaphore(vk::SemaphoreCreateInfo{});
	}
	swapChain.frameIndex = 0;
	swapChain.imageIndex = 0;
	swapChain.presentationPassRecorded = false;
}

void App::destroySwapChain(bool destroySurface) noexcept {
	swapChain.presentationFramebuffers.clear();
	for (FrameSlot& frame : swapChain.frames) {
		frame.imageAcquired.clear();
		frame.fence.clear();
		frame.commandBuffer.clear();
		frame.commandPool.clear();
	}
	swapChain.renderCompleteSemaphores.clear();
	swapChain.presentationPass.clear();
	swapChain.imageViews.clear();
	swapChain.ownedImageViews.clear();
	swapChain.handle.clear();
	swapChain.imagesInFlight.clear();
	swapChain.images.clear();
	if (destroySurface) {
		swapChain.surface.clear();
	}
}

void App::shutdown() noexcept {
	if (*device) {
		waitIdle();
	}
	activeRenderer.reset();
	lastAppliedSurfaceFormat.reset();
	destroySwapChain(true);
#ifdef RENDERING_ENGINE_USE_VULKAN_DEBUG_REPORT
	debugReport.clear();
#endif
	queue.clear();
	device.clear();
	physicalDevice.clear();
	instance.clear();
}

bool App::beginFrame() {
	FrameSlot& frame = swapChain.frames[swapChain.frameIndex];
	const vk::Result waitResult = device.waitForFences(*frame.fence, true, std::numeric_limits<uint64_t>::max());
	if (waitResult == vk::Result::eTimeout) {
		throw std::runtime_error("Timeout while waiting for renderer frame fence");
	}
	const vk::Result result = (*device).acquireNextImageKHR(
		*swapChain.handle,
		std::numeric_limits<uint64_t>::max(),
		*frame.imageAcquired,
		vk::Fence{},
		&swapChain.imageIndex);
	if (result == vk::Result::eErrorOutOfDateKHR) {
		swapChainRebuild = true;
		return false;
	}
	if (result == vk::Result::eSuboptimalKHR) {
		swapChainRebuild = true;
	}
	else if (result != vk::Result::eSuccess) {
		throw std::runtime_error("Failed to acquire the next swapchain image: " + vk::to_string(result));
	}
	if (swapChain.imagesInFlight[swapChain.imageIndex]) {
		const vk::Result imageWaitResult = device.waitForFences(
			swapChain.imagesInFlight[swapChain.imageIndex],
			true,
			std::numeric_limits<uint64_t>::max());
		if (imageWaitResult == vk::Result::eTimeout) {
			throw std::runtime_error("Timeout while waiting for a swapchain image fence");
		}
	}
	swapChain.imagesInFlight[swapChain.imageIndex] = *frame.fence;
	device.resetFences(*frame.fence);
	frame.commandPool.reset();
	frame.commandBuffer.begin(vk::CommandBufferBeginInfo{
		.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
	});
	swapChain.presentationPassRecorded = false;
	return true;
}

void App::beginPresentationPass() noexcept {
	assert(!swapChain.presentationPassRecorded && "Presentation pass may only be recorded once per frame");
	const vk::RenderPassBeginInfo beginInfo{
		.renderPass = *swapChain.presentationPass,
		.framebuffer = *swapChain.presentationFramebuffers[swapChain.imageIndex],
		.renderArea = { .extent = swapChain.extent }
	};
	swapChain.frames[swapChain.frameIndex].commandBuffer.beginRenderPass(
		beginInfo,
		vk::SubpassContents::eInline);
	swapChain.presentationPassRecorded = true;
}

void App::endPresentationPass() noexcept {
	assert(swapChain.presentationPassRecorded && "Presentation pass must be started before it is ended");
	swapChain.frames[swapChain.frameIndex].commandBuffer.endRenderPass();
}

void App::transitionCurrentImageForPresentation() noexcept {
	const vk::ImageMemoryBarrier barrier{
		.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
		.oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
		.newLayout = vk::ImageLayout::ePresentSrcKHR,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = swapChain.images[swapChain.imageIndex],
		.subresourceRange = {
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};
	swapChain.frames[swapChain.frameIndex].commandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eColorAttachmentOutput,
		vk::PipelineStageFlagBits::eBottomOfPipe,
		{},
		{},
		{},
		barrier);
}

void App::endMainFrame() {
	if (!swapChain.presentationPassRecorded) {
		transitionCurrentImageForPresentation();
	}
	FrameSlot& frame = swapChain.frames[swapChain.frameIndex];
	frame.commandBuffer.end();
	constexpr vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	const vk::Semaphore imageAcquired = *frame.imageAcquired;
	const vk::CommandBuffer commandBuffer = *frame.commandBuffer;
	const vk::Semaphore renderComplete = *swapChain.renderCompleteSemaphores[swapChain.imageIndex];
	const vk::SubmitInfo submitInfo{
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &imageAcquired,
		.pWaitDstStageMask = &waitStage,
		.commandBufferCount = 1,
		.pCommandBuffers = &commandBuffer,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &renderComplete
	};
	const auto lock = lockQueue();
	queue.submit(submitInfo, *frame.fence);
}

void App::endFrame() {
	const vk::Semaphore renderComplete = *swapChain.renderCompleteSemaphores[swapChain.imageIndex];
	const vk::SwapchainKHR handle = *swapChain.handle;
	const vk::PresentInfoKHR presentInfo{
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &renderComplete,
		.swapchainCount = 1,
		.pSwapchains = &handle,
		.pImageIndices = &swapChain.imageIndex
	};
	vk::Result result;
	{
		const auto lock = lockQueue();
		result = (*queue).presentKHR(&presentInfo);
	}
	if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
		swapChainRebuild = true;
	}
	else if (result != vk::Result::eSuccess) {
		throw std::runtime_error("Failed to present the swapchain image: " + vk::to_string(result));
	}
	swapChain.frameIndex = (swapChain.frameIndex + 1) % FramesInFlight;
}

void App::render(UniformBufferObject& ubo, bool drawScene) noexcept {
	getRenderer().setActiveCommandBuffer(currentCommandBuffer());
	getRenderer().render(ubo, swapChain.frameIndex, swapChain.imageIndex, drawScene);
}

bool App::resizeMainWindow(uint32_t width, uint32_t height) {
	swapChainRebuild = true;
	bool formatChanged = false;
	{
		const auto lock = lockQueue();
		device.waitIdle();
		getRenderer().releaseSwapChainResources();
		destroySwapChain(false);
		createSwapChain(width, height);
		formatChanged = !lastAppliedSurfaceFormat ||
			*lastAppliedSurfaceFormat != swapChain.format.format;
	}
	getRenderer().recreateSwapChainResources(formatChanged);
	lastAppliedSurfaceFormat = swapChain.format.format;
	swapChainRebuild = false;
	return formatChanged;
}

void App::submitAndWaitForFence(const vk::SubmitInfo& submitInfo) {
	const vk::FenceCreateInfo fenceInfo{};
	const vk::raii::Fence fence = device.createFence(fenceInfo);
	{
		const auto lock = lockQueue();
		queue.submit(submitInfo, *fence);
	}
	const vk::Result result = device.waitForFences(*fence, true, std::numeric_limits<uint64_t>::max());
	if (result == vk::Result::eTimeout) {
		throw std::runtime_error("Timeout while waiting for renderer fence");
	}
}

void App::waitIdle() noexcept {
	if (!*device) {
		return;
	}
	try {
		const auto lock = lockQueue();
		device.waitIdle();
	}
	catch (const std::exception& e) {
		Runtime::log(LogLevel::warning, "Exception while waiting for device idle, what: " + std::string(e.what()));
	}
}

std::unique_lock<std::mutex> App::lockQueue() {
	return std::unique_lock(queueMutex);
}

vk::Extent2D App::extent() noexcept {
	return swapChain.extent;
}

vk::Format App::surfaceFormat() noexcept {
	return swapChain.format.format;
}

uint32_t App::imageCount() noexcept { return static_cast<uint32_t>(swapChain.images.size()); }
uint32_t App::currentFrameIndex() noexcept { return swapChain.frameIndex; }
uint32_t App::currentImageIndex() noexcept { return swapChain.imageIndex; }
vk::CommandBuffer App::currentCommandBuffer() noexcept { return *swapChain.frames[swapChain.frameIndex].commandBuffer; }
vk::CommandPool App::transferCommandPool() noexcept { return *swapChain.frames.front().commandPool; }
vk::RenderPass App::presentationRenderPass() noexcept { return *swapChain.presentationPass; }
vk::Framebuffer App::currentPresentationFramebuffer() noexcept { return *swapChain.presentationFramebuffers[swapChain.imageIndex]; }
std::span<const vk::ImageView> App::swapChainImageViews() noexcept { return swapChain.imageViews; }

}
