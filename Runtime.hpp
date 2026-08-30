#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

namespace renderer {

/*
	message: a operation compleated fully but with something to note
	warning: a operation can be completed, but not fully
	error: a operation can not be completed
*/
enum class LogLevel {
	message,
	warning,
	error
};

struct RendererSettings {
	glm::vec3 backgroundColor{ 0.2f };
	float dynamicObjectViewDistance = 600.0f;
	bool shadowsEnabled = true;
	float shadowDistance = 200.0f;
	float textureFullResolutionDistance = 75.0f;
	float textureMediumResolutionDistance = 350.0f;
};

struct Configuration {
	std::string applicationName = "Rendering application";
	std::string engineName = "Rendering engine";
	uint32_t applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
	uint32_t engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
	RendererSettings renderer;
};

class Runtime {
public:
	using LogCallback = std::function<void(LogLevel, std::string_view)>;

	Runtime() = delete;

	[[nodiscard]] static Configuration& configuration() noexcept;
	static void configure(Configuration&& configuration) noexcept;

	// The log callback must be thread safe and should avoid throwing exceptions.
	static void setLogCallback(const LogCallback& callback) noexcept;
	static void log(LogLevel level, std::string_view message) noexcept;
};

}
