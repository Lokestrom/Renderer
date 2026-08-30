#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <RenderingEngine.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void glfwErrorCallback(int error, const char* description) {
	std::cerr << "GLFW error " << error << ": " << description << '\n';
}

[[nodiscard]] renderer::UniformBufferObject cameraUniforms(const renderer::Camera& camera) noexcept {
	return {
		.proj = camera.getProjection(),
		.view = camera.getView(),
		.cameraPos = glm::vec4(camera.getPosition(), 1.0f),
		.lightSource = glm::vec4(glm::normalize(glm::vec3(-0.5f, -1.0f, -0.35f)), 1.0f)
	};
}

[[nodiscard]] renderer::ID addSceneObject(
	const std::filesystem::path& modelPath,
	glm::vec3 position,
	glm::vec3 scale) {
	return renderer::GameObjectContainer::Add(renderer::GameObject{
		renderer::ModelCache::loadModel(modelPath),
		position,
		glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
		scale
	});
}

}

int main() {
	glfwSetErrorCallback(glfwErrorCallback);
	if (!glfwInit()) {
		std::cout << "Failed to initialize GLFW\n";
		return 0;
	}

	if (!glfwVulkanSupported()) {
		std::cout << "Vulkan is not supported\n";
		glfwTerminate();
		return 0;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(960, 540, "RenderingEngine GLFW consumer", nullptr, nullptr);
	if (!window) {
		glfwTerminate();
		return 1;
	}

	bool rendererStartupAttempted = false;
	renderer::ID cubeObject = 0;
	std::vector<renderer::ID> sceneObjects;
	sceneObjects.reserve(2);
	try {
		renderer::Configuration configuration;
		configuration.applicationName = "RenderingEngine GLFW consumer";
		configuration.renderer.backgroundColor = glm::vec3(0.025f, 0.04f, 0.07f);
		configuration.renderer.shadowDistance = 10.0f;
		renderer::Runtime::configure(std::move(configuration));
		renderer::Runtime::setLogCallback([](renderer::LogLevel level, std::string_view message) {
			std::ostream& output = level == renderer::LogLevel::error ? std::cerr : std::clog;
			output << "[renderer] " << message << '\n';
		});

		uint32_t extensionCount = 0;
		const char** requiredExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
		if (requiredExtensions == nullptr || extensionCount == 0) {
			throw std::runtime_error("GLFW did not provide Vulkan instance extensions");
		}
		rendererStartupAttempted = true;
		renderer::App::startup(std::vector<const char*>(requiredExtensions, requiredExtensions + extensionCount));

		VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
		if (glfwCreateWindowSurface(
			static_cast<VkInstance>(*renderer::App::instance), window, nullptr, &rawSurface) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create the GLFW Vulkan surface");
		}
		vk::raii::SurfaceKHR surface(renderer::App::instance, rawSurface);
		int framebufferWidth = 0;
		int framebufferHeight = 0;
		glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
		renderer::App::startupWindow(
			std::move(surface),
			framebufferWidth,
			framebufferHeight);

		cubeObject = addSceneObject(
			std::filesystem::path(RENDERING_ENGINE_EXAMPLE_CUBE_MODEL),
			glm::vec3(0.0f),
			glm::vec3(1.0f));
		sceneObjects.push_back(cubeObject);
		sceneObjects.push_back(addSceneObject(
			std::filesystem::path(RENDERING_ENGINE_EXAMPLE_PLANE_MODEL),
			glm::vec3(0.0f, -1.15f, 0.0f),
			glm::vec3(3.25f)));

		renderer::Camera camera;
		const glm::vec3 cameraPosition(0.0f, 2.4f, -5.0f);
		const glm::mat4 cameraView = glm::lookAtLH(
			cameraPosition,
			glm::vec3(0.0f, -0.25f, 0.0f),
			glm::vec3(0.0f, -1.0f, 0.0f));
		camera.setTransform(
			cameraPosition,
			glm::normalize(glm::quat_cast(glm::mat3(glm::inverse(cameraView)))));
		const auto startedAt = std::chrono::steady_clock::now();

		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();
			glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
			if (framebufferWidth == 0 || framebufferHeight == 0) {
				glfwWaitEvents();
				continue;
			}

			const vk::Extent2D extent = renderer::App::extent();
			if (renderer::App::swapChainRebuild ||
				extent.width != static_cast<uint32_t>(framebufferWidth) ||
				extent.height != static_cast<uint32_t>(framebufferHeight)) {
				(void)renderer::App::resizeMainWindow(framebufferWidth, framebufferHeight);
			}

			camera.setPerspective(
				glm::radians(60.0f),
				static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight),
				0.01f,
				100.0f);
			const float seconds = std::chrono::duration<float>(
				std::chrono::steady_clock::now() - startedAt).count();
			renderer::GameObjectContainer::get(cubeObject).orientation = glm::angleAxis(
				seconds,
				glm::normalize(glm::vec3(0.35f, 1.0f, 0.2f)));

			if (!renderer::App::beginFrame()) {
				continue;
			}
			renderer::UniformBufferObject uniforms = cameraUniforms(camera);
			renderer::App::render(uniforms);
			renderer::App::endMainFrame();
			renderer::App::endFrame();
		}

		renderer::GameObjectContainer::remove(sceneObjects);
		sceneObjects.clear();
		cubeObject = 0;
		renderer::App::shutdown();
		rendererStartupAttempted = false;
	}
	catch (const std::exception& exception) {
		std::cerr << "External renderer consumer failed: " << exception.what() << '\n';
		if (!sceneObjects.empty()) {
			renderer::GameObjectContainer::remove(sceneObjects);
		}
		if (rendererStartupAttempted) {
			try {
				renderer::App::shutdown();
			}
			catch (...) {
			}
		}
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
