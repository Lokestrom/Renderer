#include "VulkanApp.hpp"
#include "VulkanApp.hpp"

#include "Renderer.hpp"

#include "../App.hpp"

namespace vulkan {

static void check_vk_result(VkResult err) {
	if (err == VK_SUCCESS)
		return;
	__debugbreak();
	fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
	if (err < 0)
		abort();
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

static bool IsExtensionAvailable(const ImVector<vk::ExtensionProperties>& properties, const char* extension) {
	for (const vk::ExtensionProperties& p : properties)
		if (strcmp(p.extensionName, extension) == 0)
			return true;
	return false;
}

void App::startup(ImVector<const char*> instance_extensions) {
	vk::Result result;

	{
		vk::InstanceCreateInfo create_info;

		uint32_t properties_count;
		ImVector<vk::ExtensionProperties> properties;
		result = vk::enumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
		check_vk_result(result);
		properties.resize(properties_count);
		result = vk::enumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
		check_vk_result(result);

		if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
			instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
		if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
			instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
			create_info.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
		}
#endif

#ifdef APP_USE_VULKAN_DEBUG_REPORT
		const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
		create_info.enabledLayerCount = 1;
		create_info.ppEnabledLayerNames = layers;
		instance_extensions.push_back("VK_EXT_debug_report");
#endif

		create_info.enabledExtensionCount = (uint32_t)instance_extensions.Size;
		create_info.ppEnabledExtensionNames = instance_extensions.Data;
		result = vk::createInstance(&create_info, nullptr, &instance);
		check_vk_result(result);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
		VkResult err;
		auto f_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
		IM_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
		VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
		debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
		debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
		debug_report_ci.pfnCallback = debug_report;
		debug_report_ci.pUserData = nullptr;
		err = f_vkCreateDebugReportCallbackEXT(instance, &debug_report_ci, nullptr, &debugReport);
		check_vk_result(err);
#endif
	}

	physicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(instance);
	IM_ASSERT(physicalDevice != VK_NULL_HANDLE);

	queueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(physicalDevice);
	IM_ASSERT(queueFamily != (uint32_t)-1);

	{
		ImVector<const char*> device_extensions;
		device_extensions.push_back("VK_KHR_swapchain");

		uint32_t properties_count;
		ImVector<vk::ExtensionProperties> properties;
		result = physicalDevice.enumerateDeviceExtensionProperties(nullptr, &properties_count, nullptr);
		check_vk_result(result);

		properties.resize(properties_count);
		result = physicalDevice.enumerateDeviceExtensionProperties(nullptr, &properties_count, properties.Data);
		check_vk_result(result);


#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
		if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
			device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

		const float queue_priority[] = { 1.0f };
		vk::DeviceQueueCreateInfo queueCreateInfo[1];
		queueCreateInfo[0].queueFamilyIndex = queueFamily;
		queueCreateInfo[0].queueCount = 1;
		queueCreateInfo[0].pQueuePriorities = queue_priority;
		vk::DeviceCreateInfo deviceCreateInfo;
		deviceCreateInfo.queueCreateInfoCount = sizeof(queueCreateInfo) / sizeof(queueCreateInfo[0]);
		deviceCreateInfo.pQueueCreateInfos = queueCreateInfo;
		deviceCreateInfo.enabledExtensionCount = (uint32_t)device_extensions.Size;
		deviceCreateInfo.ppEnabledExtensionNames = device_extensions.Data;
		result = physicalDevice.createDevice(&deviceCreateInfo, nullptr, &device);
		check_vk_result(result);
		queue = device.getQueue(queueFamily, 0);
	}

	{
		vk::DescriptorPoolSize pool_sizes[] = {
			{ vk::DescriptorType::eCombinedImageSampler, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE },
		};
		vk::DescriptorPoolCreateInfo pool_info = {};
		pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		pool_info.maxSets = 0;
		for (vk::DescriptorPoolSize& pool_size : pool_sizes)
			pool_info.maxSets += pool_size.descriptorCount;
		pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
		pool_info.pPoolSizes = pool_sizes;
		result = device.createDescriptorPool(&pool_info, nullptr, &descriptorPool);
		check_vk_result(result);
	}
}

void App::startupWindow(ImGui_ImplVulkanH_Window* wd, vk::SurfaceKHR surface, int width, int height) {
	wd->Surface = surface;
	wd->ClearEnable = false;

	VkBool32 res;
	res = physicalDevice.getSurfaceSupportKHR(queueFamily, wd->Surface);
	if (res != VK_TRUE) {
		fprintf(stderr, "Error no WSI support on physical device 0\n");
		exit(-1);
	}

	const VkFormat requestSurfaceImageFormat[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
	const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
	wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(physicalDevice, wd->Surface, requestSurfaceImageFormat, (size_t)IM_ARRAYSIZE(requestSurfaceImageFormat), requestSurfaceColorSpace);

#ifdef APP_USE_UNLIMITED_FRAME_RATE
	VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
#else
	VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
#endif
	wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(physicalDevice, wd->Surface, &present_modes[0], IM_ARRAYSIZE(present_modes));

	IM_ASSERT(minImageCount >= 2);
	ImGui_ImplVulkanH_CreateOrResizeWindow(instance, physicalDevice, device, wd, queueFamily, nullptr, width, height, minImageCount);


	renderer = new Renderer();
}


void App::shutdown() {
	device.waitIdle();

	delete renderer;

	ImGui_ImplVulkanH_DestroyWindow(instance, device, &mainWindowData, nullptr);

	device.destroyDescriptorPool(descriptorPool);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
	auto f_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
	f_vkDestroyDebugReportCallbackEXT(instance, debugReport, nullptr);
#endif

	device.destroy();
	instance.destroy();
}


void vulkan::App::beginFrame(ImGui_ImplVulkanH_Window* wd) {
	vk::Semaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
	vk::Result result;
	result = device.acquireNextImageKHR(wd->Swapchain, UINT64_MAX, image_acquired_semaphore, nullptr, &wd->FrameIndex);
	if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
		swapChainRebuild = true;
	if (result == vk::Result::eErrorOutOfDateKHR)
		return;
	if (result != vk::Result::eSuboptimalKHR)
		check_vk_result(result);


	ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
	{
		result = device.waitForFences(
			1,
			reinterpret_cast<const vk::Fence*>(&fd->Fence),
			VK_TRUE,
			std::numeric_limits<uint64_t>::max());
		check_vk_result(result);

		result = device.resetFences(1, reinterpret_cast<vk::Fence*>(&fd->Fence));
		check_vk_result(result);
	}
	{
		try {
			device.resetCommandPool(static_cast<vk::CommandPool>(fd->CommandPool));
		}
		catch (std::exception& e) {
			throw std::runtime_error("Failed to reset command pool, with error: \n");
		}
		vk::CommandBufferBeginInfo info;
		info.flags |= vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		result = static_cast<vk::CommandBuffer>(fd->CommandBuffer).begin(&info);
		check_vk_result(result);
	}
}

void App::endMainFrame(ImGui_ImplVulkanH_Window* wd) {
	vk::Semaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
	vk::Semaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

	vk::Result result;
	ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];

	{
		vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		vk::SubmitInfo info;
		info.waitSemaphoreCount = 1;
		info.pWaitSemaphores = &image_acquired_semaphore;
		info.pWaitDstStageMask = &wait_stage;
		info.commandBufferCount = 1;
		info.pCommandBuffers = reinterpret_cast<vk::CommandBuffer*>(&fd->CommandBuffer);
		info.signalSemaphoreCount = 1;
		info.pSignalSemaphores = &render_complete_semaphore;

		try {
			static_cast<vk::CommandBuffer>(fd->CommandBuffer).end();
		}
		catch (std::exception& e) {
			throw std::runtime_error("Failed to end command buffer, with error: ");
		}
		result = queue.submit(1, &info, static_cast<vk::Fence>(fd->Fence));
		check_vk_result(result);
	}
}

void App::endFrame(ImGui_ImplVulkanH_Window* wd) {
	if (swapChainRebuild)
		return;
	vk::Result result;
	vk::Semaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
	vk::PresentInfoKHR info;
	info.waitSemaphoreCount = 1;
	info.pWaitSemaphores = &render_complete_semaphore;
	info.swapchainCount = 1;
	info.pSwapchains = reinterpret_cast<vk::SwapchainKHR*>(&wd->Swapchain);
	info.pImageIndices = &wd->FrameIndex;
	result = queue.presentKHR(info);
	if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
		swapChainRebuild = true;
	if (result == vk::Result::eErrorOutOfDateKHR)
		return;
	if (result != vk::Result::eSuboptimalKHR)
		check_vk_result(result);
	wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
}

void App::render(UniformBufferObject& ubo) {
	renderer->setActiveCommandBuffer(mainWindowData.Frames[mainWindowData.FrameIndex].CommandBuffer);
	renderer->render(ubo, mainWindowData.FrameIndex);
}

void App::rebuild() {
	renderer->recreate();
}
}