#include "Model.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <external/tiny_obj_loader.h>

#include "Renderer.hpp"
#include "helpers.hpp"
#include "../console.hpp"

#include <unordered_map>
#include <iostream>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace vulkan {

std::array<vk::VertexInputBindingDescription, 1> Model3D::Vertex::bindingDescriptions() noexcept {
	std::array<vk::VertexInputBindingDescription, 1> descriptions{ { 
		{ .binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex }
	} };

	return descriptions;
}
std::array<vk::VertexInputAttributeDescription, 4> Model3D::Vertex::attributeDescriptions() noexcept {
	std::array<vk::VertexInputAttributeDescription, 4> descriptions = { { 
		{ .location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, position) },
		{ .location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, color) },
		{ .location = 2, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, normal) },
		{ .location = 3, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(Vertex, uv) }
	} };

	return descriptions;
}

Model3D::Model3D(Model3D&& other) noexcept 
	: vertexCount(other.vertexCount),
	  vertexBuffer(other.vertexBuffer),
	  vertexMemory(other.vertexMemory), 
	  texture(other.texture) {
	other.vertexCount = 0;
	other.vertexBuffer = nullptr;
	other.vertexMemory = nullptr;
	other.texture = 0;
}

Model3D& Model3D::operator=(Model3D&& other) noexcept {
	if (this == &other)
		return *this;
	
	destroy();
	vertexCount = other.vertexCount;
	vertexBuffer = other.vertexBuffer;
	vertexMemory = other.vertexMemory;
	texture = other.texture;
	other.vertexCount = 0;
	other.vertexBuffer = nullptr;
	other.vertexMemory = nullptr;
	other.texture = 0;
	return *this;
}

Model3D::Model3D(std::filesystem::path file, CreationTransform transform) 
	: texture(0)
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, file.string().c_str(), file.parent_path().string().c_str())) {
		throw std::runtime_error(warn + err);
	}
	if (!warn.empty())
		Console::log(Console::Type::warning, "Tiny obj loader: " + warn);

	std::vector<Vertex> vertices;
	vertexCount = 0;

	for (const auto& shape : shapes) {
		vertexCount += shape.mesh.indices.size();
	}

	vertices.reserve(vertexCount);

	for (const auto& shape : shapes) {
		const auto& indices = shape.mesh.indices;

		for (size_t i = 0; i < indices.size(); i += 3) {
			Vertex v[3]{};

			for (int k = 0; k < 3; ++k) {
				const auto& index = indices[i + k];

				glm::vec3 pos{
					attrib.vertices[3 * index.vertex_index + 0],
					attrib.vertices[3 * index.vertex_index + 1],
					attrib.vertices[3 * index.vertex_index + 2],
				};

				pos = glm::rotate(transform.rotation, pos);
				pos *= transform.scale;
				pos += transform.position;

				v[k].position = pos;

				if (!attrib.colors.empty()) {
					v[k].color = {
						attrib.colors[3 * index.vertex_index + 0] * (1 - transform.color.w) + transform.color.x * transform.color.w,
						attrib.colors[3 * index.vertex_index + 1] * (1 - transform.color.w) + transform.color.y * transform.color.w,
						attrib.colors[3 * index.vertex_index + 2] * (1 - transform.color.w) + transform.color.z * transform.color.w,
					};
				}

				if (!attrib.texcoords.empty() && index.texcoord_index >= 0) {
					v[k].uv = {
						attrib.texcoords[2 * index.texcoord_index + 0],
						1.0f - attrib.texcoords[2 * index.texcoord_index + 1],
					};
				}
				else {
					v[k].uv = { 0.0f, 0.0f };
				}
			}

			//cant trust obj file nonmales
			glm::vec3 e1 = v[1].position - v[0].position;
			glm::vec3 e2 = v[2].position - v[0].position;

			glm::vec3 normal = glm::normalize(glm::cross(e1, e2));

			v[0].normal = normal;
			v[1].normal = normal;
			v[2].normal = normal;

			vertices.push_back(v[0]);
			vertices.push_back(v[1]);
			vertices.push_back(v[2]);
		}
	}


	assert(vertices.size() == vertexCount && "vertex count is wrong");
	assert(vertices.size() == vertices.capacity() && "Allocated wrong size");

	vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

	vk::BufferCreateInfo vertexBufferInfo{
		.size = bufferSize,
		.usage = vk::BufferUsageFlagBits::eVertexBuffer,
		.sharingMode = vk::SharingMode::eExclusive
	};

	vkCheck(App::device.createBuffer(&vertexBufferInfo, nullptr, &vertexBuffer));

	vk::MemoryRequirements memory_requirements;
	App::device.getBufferMemoryRequirements(vertexBuffer, &memory_requirements);

	vk::PhysicalDeviceMemoryProperties mem_properties;
	mem_properties = App::physicalDevice.getMemoryProperties();

	uint32_t memoryIndex = (uint32_t)-1;

	for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
		if (memory_requirements.memoryTypeBits & (1 << i)) {
			if ((mem_properties.memoryTypes[i].propertyFlags &
					(vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)) ==
				(vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)) {
				memoryIndex = i;
				break;
			}
		}
	}
	if (memoryIndex == (uint32_t)-1)
		throw std::runtime_error("Failed to find suitable memory type");


	vk::MemoryAllocateInfo alloc_info{
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = memoryIndex
	};

	vkCheck(App::device.allocateMemory(&alloc_info, nullptr, &vertexMemory));

	App::device.bindBufferMemory(vertexBuffer, vertexMemory, 0);

	void* data;
	vkCheck(App::device.mapMemory(vertexMemory, 0, bufferSize, {}, &data));
	memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
	App::device.unmapMemory(vertexMemory);

	assert(materials.size() < 2 && "Currently does not support multiple materials");

	// when adding a debug console give error for more than 1 material
	// and warnings if a material has other textures other than diffuse
	if (materials.size() != 0)
		texture = TextureCache::loadTexture(file.parent_path() / std::filesystem::path(materials[0].diffuse_texname));
}

Model3D::~Model3D() {
	destroy();
}

void Model3D::destroy() {
	if (vertexBuffer) {
		assert(vertexCount != 0 && "A buffer must contain vertices");
		assert(vertexMemory != nullptr && "Must have memory to have buffer");

		try {
			App::device.waitIdle();
		}
		catch(std::exception&) {
			throw std::runtime_error("Failed to wait for idle when destroying model 3d");
		}

		App::device.destroyBuffer(vertexBuffer);
		App::device.freeMemory(vertexMemory);
		TextureCache::unloadTexture(texture);
	}
}

void Model3D::bind(vk::CommandBuffer cmd) const noexcept {
	vk::Buffer buffers[] = { vertexBuffer };
	vk::DeviceSize offsets[] = { 0 };
	cmd.bindVertexBuffers(0, 1, buffers, offsets);
}
void Model3D::draw(vk::CommandBuffer cmd) const noexcept {
	cmd.draw(vertexCount, 1, 0, 0);
}

Model3D& ModelCache::getModel(ID id) {
	assert(_cache.contains(id) && "ModelCache does not contain model with given id");
	return _cache.at(id);
}

ID ModelCache::loadModel(std::filesystem::path file, Model3D::CreationTransform transform) {
	if (_idMap.contains(std::make_pair(file, transform))) {
		ID id = _idMap.at(std::make_pair(file, transform));
		_refCounts[id]++;
		return id;
	}
	
	ID id;
	do {
		id = rand() + 1;
	} while (_cache.contains(id));

	try {
	_cache.emplace(id, Model3D(file, transform));
	}
	catch(std::exception& e) {
		Console::Log(Console::Type::error, std::string("Failed to create model: ") + e.what());
		return 0;
	}
	_idMap[std::make_pair(file, transform)] = id;
	_refCounts[id] = 1;
	return id;
}

void ModelCache::unloadModel(ID id) {
	_refCounts[id]--;
	assert(_refCounts[id] >= 0 && "Model reference count cannot be negative");
	if (_refCounts[id] == 0) {
		_cache.erase(id);
		_refCounts.erase(id);
		for (auto it = _idMap.begin(); it != _idMap.end(); ++it) {
			if (it->second == id) {
				_idMap.erase(it);
				break;
			}
		}
	}
}

}