#pragma once

#include <array>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

namespace renderer {
struct UniformBufferObject;

class App {
public:
	static constexpr uint32_t FramesInFlight = 2;

	App() = delete;
	App(const App&) = delete;
	App& operator=(const App&) = delete;
	App(App&&) = delete;
	App& operator=(App&&) = delete;

	static void startup(std::vector<const char*> instanceExtensions);
	static void startupWindow(vk::raii::SurfaceKHR surface, uint32_t width, uint32_t height);
	static void shutdown() noexcept;

	[[nodiscard]] static bool beginFrame();
	static void beginPresentationPass() noexcept;
	static void endPresentationPass() noexcept;
	static void endMainFrame();
	static void endFrame();
	static void render(UniformBufferObject& ubo, bool drawScene = true) noexcept;

	[[nodiscard]] static bool resizeMainWindow(uint32_t width, uint32_t height);
	static void submitAndWaitForFence(const vk::SubmitInfo& submitInfo);
	static void waitIdle() noexcept;
	[[nodiscard]] static std::unique_lock<std::mutex> lockQueue();

	[[nodiscard]] static vk::Extent2D extent() noexcept;
	[[nodiscard]] static vk::Format surfaceFormat() noexcept;
	[[nodiscard]] static uint32_t imageCount() noexcept;
	[[nodiscard]] static uint32_t currentFrameIndex() noexcept;
	[[nodiscard]] static uint32_t currentImageIndex() noexcept;
	[[nodiscard]] static vk::CommandBuffer currentCommandBuffer() noexcept;
	[[nodiscard]] static vk::CommandPool transferCommandPool() noexcept;
	[[nodiscard]] static vk::RenderPass presentationRenderPass() noexcept;
	[[nodiscard]] static vk::Framebuffer currentPresentationFramebuffer() noexcept;
	[[nodiscard]] static std::span<const vk::ImageView> swapChainImageViews() noexcept;

	static inline vk::raii::Context context;
	static inline vk::raii::Instance instance = nullptr;
	static inline vk::raii::PhysicalDevice physicalDevice = nullptr;
	static inline vk::raii::Device device = nullptr;
	static inline uint32_t queueFamily = UINT32_MAX;
	static inline vk::raii::Queue queue = nullptr;
	static inline vk::raii::DebugReportCallbackEXT debugReport = nullptr;
	static inline bool swapChainRebuild = false;

private:
	struct FrameSlot {
		vk::raii::CommandPool commandPool = nullptr;
		vk::raii::CommandBuffer commandBuffer = nullptr;
		vk::raii::Fence fence = nullptr;
		vk::raii::Semaphore imageAcquired = nullptr;
	};

	struct SwapChainState {
		vk::raii::SurfaceKHR surface = nullptr;
		vk::SurfaceFormatKHR format{};
		vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
		vk::raii::SwapchainKHR handle = nullptr;
		vk::raii::RenderPass presentationPass = nullptr;
		vk::Extent2D extent{};
		std::vector<vk::Image> images;
		std::vector<vk::raii::ImageView> ownedImageViews;
		std::vector<vk::ImageView> imageViews;
		std::vector<vk::raii::Framebuffer> presentationFramebuffers;
		std::vector<vk::Fence> imagesInFlight;
		std::vector<vk::raii::Semaphore> renderCompleteSemaphores;
		std::array<FrameSlot, FramesInFlight> frames;
		uint32_t frameIndex = 0;
		uint32_t imageIndex = 0;
		bool presentationPassRecorded = false;
	};

private:
	static void createSwapChain(uint32_t width, uint32_t height);
	static void destroySwapChain(bool destroySurface) noexcept;
	static void transitionCurrentImageForPresentation() noexcept;

private:
	static inline SwapChainState swapChain;
	static inline std::optional<vk::Format> lastAppliedSurfaceFormat;
	static inline std::mutex queueMutex;
};

}
