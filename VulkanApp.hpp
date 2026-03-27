#pragma once

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_glfw.h"
#include "ImGui/imgui_impl_vulkan.h"

#include <memory>

namespace vulkan {
class Renderer;
struct UniformBufferObject;

class App {
public:
	App() = delete;

	App(App&) = delete;
	App& operator=(App&) = delete;

	App(App&&) = delete;
	App& operator=(App&&) = delete;

	static void startup(ImVector<const char*> instance_extensions);
	static void startupWindow(ImGui_ImplVulkanH_Window* wd, vk::SurfaceKHR surface, int width, int height);
	static void shutdown();

	static void beginFrame(ImGui_ImplVulkanH_Window* wd);
	static void endFrame(ImGui_ImplVulkanH_Window* wd);
	static void render(UniformBufferObject& ubo);

public:
#ifdef _DEBUG
#define APP_USE_VULKAN_DEBUG_REPORT
	static inline VkDebugReportCallbackEXT debugReport = nullptr;
#endif

	static inline vk::Instance instance = nullptr;
	static inline vk::PhysicalDevice physicalDevice = nullptr;
	static inline vk::Device device = nullptr;
	static inline uint32_t queueFamily = (uint32_t)-1;
	static inline vk::Queue queue = nullptr;
	static inline vk::PipelineCache pipelineCache = nullptr;
	static inline vk::DescriptorPool descriptorPool = nullptr;

	static inline ImGui_ImplVulkanH_Window mainWindowData;
	static inline uint32_t minImageCount = 2;
	static inline bool swapChainRebuild = false;

private:
	static inline Renderer* renderer;
};

}