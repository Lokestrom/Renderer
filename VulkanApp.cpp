#include "VulkanApp.hpp"

#include "Renderer.hpp"
#include "../App.hpp"

#include <iostream>

namespace vulkan {

void createRenderingSettings() {
	createCameraSettings();
}

static void check_vk_result(vk::Result err) {
	if (err == vk::Result::eSuccess)
		return;
	__debugbreak();
	fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
	if (err < vk::Result::eSuccess)
		abort();
}

#ifdef APP_USE_VULKAN_DEBUG_REPORT
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData) {
	(void)flags;
	(void)object;
	(void)location;
	(void)messageCode;
	(void)pUserData;
	(void)pLayerPrefix;
	fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
	return VK_FALSE;
}
#endif

static bool IsExtensionAvailable(const std::vector<vk::ExtensionProperties>& properties, const char* extension) {
	for (const vk::ExtensionProperties& p : properties)
		if (std::strcmp(p.extensionName, extension) == 0)
			return true;
	return false;
}

void App::startup(std::vector<const char*> instanceExtensions) {

	{
		vk::InstanceCreateInfo InstanceCreateInfo;

		std::vector<vk::ExtensionProperties> properties = context.enumerateInstanceExtensionProperties();

		if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
			instanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
		if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
			instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
			InstanceCreateInfo.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
		}
#endif

#ifdef APP_USE_VULKAN_DEBUG_REPORT
		constexpr std::array<const char*, 1> layers = { "VK_LAYER_KHRONOS_validation" };
		InstanceCreateInfo.enabledLayerCount = 1;
		InstanceCreateInfo.ppEnabledLayerNames = layers.data();
		instanceExtensions.push_back("VK_EXT_debug_report");
		instanceExtensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);

		std::array<vk::ValidationFeatureEnableEXT, 3> enables = {
			vk::ValidationFeatureEnableEXT::eGpuAssisted,
			vk::ValidationFeatureEnableEXT::eGpuAssistedReserveBindingSlot,
			vk::ValidationFeatureEnableEXT::eSynchronizationValidation
		};

		vk::ValidationFeaturesEXT features = {
			.enabledValidationFeatureCount = static_cast<uint32_t>(enables.size()),
			.pEnabledValidationFeatures = enables.data()
		};
		InstanceCreateInfo.pNext = &features;
#endif

		InstanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
		InstanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
		instance = context.createInstance(InstanceCreateInfo);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
		vk::DebugReportCallbackCreateInfoEXT debugReportCreateInfor{};
		debugReportCreateInfor.flags =
			vk::DebugReportFlagBitsEXT::eError |
			vk::DebugReportFlagBitsEXT::eWarning |
			vk::DebugReportFlagBitsEXT::ePerformanceWarning;
		debugReportCreateInfor.pfnCallback = reinterpret_cast<vk::PFN_DebugReportCallbackEXT>(debug_report);
		debugReportCreateInfor.pUserData = nullptr;
		debugReport = instance.createDebugReportCallbackEXT(debugReportCreateInfor);
#endif
	}

	VkPhysicalDevice selectedPhysicalDevice =
		ImGui_ImplVulkanH_SelectPhysicalDevice(static_cast<VkInstance>(*instance));
	physicalDevice = vk::raii::PhysicalDevice(instance, selectedPhysicalDevice);
	assert(*physicalDevice && "Failed to select physical device");

	queueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(static_cast<VkPhysicalDevice>(*physicalDevice));
	assert(queueFamily != (uint32_t)-1 && "Failed to select queue family");

	{
		std::vector<const char*> deviceExtensions;
		deviceExtensions.push_back("VK_KHR_swapchain");

		std::vector<vk::ExtensionProperties> properties = physicalDevice.enumerateDeviceExtensionProperties();


#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
		if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
			deviceExtensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

		std::array<float, 1> queuePriority = { 1.0f };
		std::array<vk::DeviceQueueCreateInfo, 1> queueCreateInfo;
		queueCreateInfo[0].queueFamilyIndex = queueFamily;
		queueCreateInfo[0].queueCount = queuePriority.size();
		queueCreateInfo[0].pQueuePriorities = queuePriority.data();
		vk::DeviceCreateInfo deviceCreateInfo;
		deviceCreateInfo.queueCreateInfoCount = queueCreateInfo.size();
		deviceCreateInfo.pQueueCreateInfos = queueCreateInfo.data();
		deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
		deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

		vk::PhysicalDeviceFeatures2 features{
			.features = vk::PhysicalDeviceFeatures{
				.shaderSampledImageArrayDynamicIndexing = true
			}
		};

		deviceCreateInfo.pNext = features;

		device = physicalDevice.createDevice(deviceCreateInfo);
		queue = device.getQueue(queueFamily, 0);
	}

	{
		constexpr std::array<const vk::DescriptorPoolSize, 1> poolSizes = {
			{ vk::DescriptorType::eCombinedImageSampler, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE },
		};
		vk::DescriptorPoolCreateInfo poolCreateInfo = {};
		poolCreateInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolCreateInfo.maxSets = 0;
		for (const vk::DescriptorPoolSize& poolSize : poolSizes)
			poolCreateInfo.maxSets += poolSize.descriptorCount;
		poolCreateInfo.poolSizeCount = poolSizes.size();
		poolCreateInfo.pPoolSizes = poolSizes.data();
		descriptorPool = device.createDescriptorPool(poolCreateInfo);
	}

}

void App::startupWindow(ImGui_ImplVulkanH_Window* wd, vk::SurfaceKHR surface, const int width, const int height) {
	assert(wd && "Window data must not be null");
	assert(surface && "Surface must not be null");
	assert(width > 0 && height > 0 && "Width and height must be greater than zero");
	wd->Surface = surface;
	wd->ClearEnable = false;

	const VkBool32 res = physicalDevice.getSurfaceSupportKHR(queueFamily, wd->Surface);
	if (res == VK_FALSE) {
		throw std::runtime_error("Selected GPU does not support rendering to the given surface");
	}

	constexpr std::array<const VkFormat, 4> requestSurfaceImageFormat = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
	const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
	wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
		static_cast<VkPhysicalDevice>(*physicalDevice),
		wd->Surface,
		requestSurfaceImageFormat.data(),
		requestSurfaceImageFormat.size(),
		requestSurfaceColorSpace);

#ifdef APP_USE_UNLIMITED_FRAME_RATE
	constexpr std::array<const VkPresentModeKHR, 3> presentModes = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
#else
	constexpr std::array<const VkPresentModeKHR, 1> presentModes = { VK_PRESENT_MODE_FIFO_KHR };
#endif
	wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
		static_cast<VkPhysicalDevice>(*physicalDevice),
		wd->Surface,
		presentModes.data(),
		presentModes.size());

	assert(minImageCount >= 2 && "Minimum image count must be at least 2");
	ImGui_ImplVulkanH_CreateOrResizeWindow(
		static_cast<VkInstance>(*instance),
		static_cast<VkPhysicalDevice>(*physicalDevice),
		static_cast<VkDevice>(*device),
		wd,
		queueFamily,
		nullptr,
		width,
		height,
		minImageCount);

	renderer = new Renderer();
}


void App::shutdown() {
	device.waitIdle();

	delete renderer;

	ImGui_ImplVulkanH_DestroyWindow(
		static_cast<VkInstance>(*instance),
		static_cast<VkDevice>(*device),
		&mainWindowData,
		nullptr);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
	debugReport.clear();
#endif

	descriptorPool.clear();
	pipelineCache.clear();
	queue.clear();
	device.clear();
	physicalDevice.clear();
	instance.clear();
}


void vulkan::App::beginFrame(ImGui_ImplVulkanH_Window* wd) {
	const vk::Semaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;

	vk::Result result = (*device).acquireNextImageKHR(wd->Swapchain, std::numeric_limits<uint64_t>::max(), image_acquired_semaphore, nullptr, &wd->FrameIndex);
	if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
		swapChainRebuild = true;
	if (result == vk::Result::eErrorOutOfDateKHR)
		return;
	if (result != vk::Result::eSuboptimalKHR)
		check_vk_result(result);

	ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
	result = device.waitForFences(*(reinterpret_cast<vk::Fence*>(&fd->Fence)), true, std::numeric_limits<uint64_t>::max());
	if (result == vk::Result::eTimeout)
		throw std::runtime_error("Timeout while waiting for fence");
	device.resetFences(*(reinterpret_cast<vk::Fence*>(&fd->Fence)));
	(*device).resetCommandPool(static_cast<vk::CommandPool>(fd->CommandPool));

	const vk::CommandBufferBeginInfo info {
		.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
	};
	static_cast<vk::CommandBuffer>(fd->CommandBuffer).begin(info);
}

void App::endMainFrame(ImGui_ImplVulkanH_Window* wd) {
	const vk::Semaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
	const vk::Semaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

	ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];

	const vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	const vk::SubmitInfo info {
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &image_acquired_semaphore,
		.pWaitDstStageMask = &wait_stage,
		.commandBufferCount = 1,
		.pCommandBuffers = reinterpret_cast<vk::CommandBuffer*>(&fd->CommandBuffer),
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &render_complete_semaphore
	};

	static_cast<vk::CommandBuffer>(fd->CommandBuffer).end();
	queue.submit(info, static_cast<vk::Fence>(fd->Fence));
}

void App::endFrame(ImGui_ImplVulkanH_Window* wd) {
	if (swapChainRebuild)
		return;
	const vk::Semaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
	const vk::PresentInfoKHR info{
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &render_complete_semaphore,
		.swapchainCount = 1,
		.pSwapchains = reinterpret_cast<vk::SwapchainKHR*>(&wd->Swapchain),
		.pImageIndices = &wd->FrameIndex
	};
	const vk::Result result = queue.presentKHR(info);
	if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
		swapChainRebuild = true;
	if (result == vk::Result::eErrorOutOfDateKHR)
		return;
	if (result != vk::Result::eSuboptimalKHR)
		check_vk_result(result);
	wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
}

void App::render(UniformBufferObject& ubo) noexcept {
	renderer->setActiveCommandBuffer(mainWindowData.Frames[mainWindowData.FrameIndex].CommandBuffer);
	renderer->render(ubo, mainWindowData.FrameIndex);
}

void App::rebuild() {
	renderer->recreate();
}
}
