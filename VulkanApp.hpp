#pragma once

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_glfw.h"
#include "ImGui/imgui_impl_vulkan.h"

#include <memory>
#include <vulkan/vulkan_raii.hpp>

#ifdef _DEBUG
#define APP_USE_VULKAN_DEBUG_REPORT
#endif

namespace vulkan {
class Renderer;
struct UniformBufferObject;

void createRenderingSettings();

class App {
public:
	App() = delete;

	App(App&) = delete;
	App& operator=(App&) = delete;

	App(App&&) = delete;
	App& operator=(App&&) = delete;

	static void startup(std::vector<const char*> instance_extensions);
	static void startupWindow(ImGui_ImplVulkanH_Window* wd, vk::SurfaceKHR surface, const int width, const int height);
	static void shutdown();

	static void beginFrame(ImGui_ImplVulkanH_Window* wd);
	static void endMainFrame(ImGui_ImplVulkanH_Window* wd);
	static void endFrame(ImGui_ImplVulkanH_Window* wd);
	static void render(UniformBufferObject& ubo) noexcept;

	static void rebuild();

public:

	static inline vk::raii::Context context;
	static inline vk::raii::Instance instance = nullptr;
	static inline vk::raii::PhysicalDevice physicalDevice = nullptr;
	static inline vk::raii::Device device = nullptr;
	static inline uint32_t queueFamily = (uint32_t)-1;
	static inline vk::raii::Queue queue = nullptr;
	static inline vk::raii::PipelineCache pipelineCache = nullptr;
	static inline vk::raii::DescriptorPool descriptorPool = nullptr;
#ifdef APP_USE_VULKAN_DEBUG_REPORT
	static inline vk::raii::DebugReportCallbackEXT debugReport = nullptr;
#endif

	static inline ImGui_ImplVulkanH_Window mainWindowData;
	static inline uint32_t minImageCount = 2;
	static inline bool swapChainRebuild = false;

private:
	static inline Renderer* renderer;
};

}