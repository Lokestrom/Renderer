#pragma once

#include "VulkanApp.hpp"

#include <filesystem>
#include <array>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace vulkan {

class Model3D {
public:
	struct Vertex {
		glm::vec3 position;
		glm::vec3 color;
		// TODO: implement normals and textures
		glm::vec3 normal;
		// glm::vec2 uv;

		static std::array<vk::VertexInputBindingDescription, 1> bindingDescriptions() noexcept;
		static std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions() noexcept;
	};

	struct CreationTransform {
		glm::vec3 position;
		glm::vec3 scale = glm::vec3{ 1.0 };
		glm::quat rotation;
		glm::vec4 color;
	};

	Model3D() : vertexCount(0), vertexBuffer(nullptr), vertexMemory(nullptr) {}

	Model3D(Model3D&) = delete;
	Model3D& operator=(Model3D&) = delete;

	Model3D(Model3D&&) noexcept;
	Model3D& operator=(Model3D&&) noexcept;

	Model3D(std::filesystem::path file, CreationTransform transform);

	~Model3D();

	void bind(vk::CommandBuffer cmd) const noexcept;
	void draw(vk::CommandBuffer cmd) const noexcept;

private:

	void destroy();

private:
	uint32_t vertexCount;
	vk::Buffer vertexBuffer;
	vk::DeviceMemory vertexMemory;

	// TODO: implement index buffers
	// uint32_t indexCount;
	// vk::DeviceMemory indexMemory;
	// vk::Buffer indexBuffer;
};

}