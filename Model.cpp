#include "Model.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <external/tiny_obj_loader.h>

#include "Renderer.hpp"
#include "helpers.hpp"
#include "../console.hpp"

#include <unordered_map>
#include <iostream>
#include <set>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "../Settings.hpp"

// TODO: create settings
// render distance, unload distance, mipmaps, mipmap distance

namespace vulkan {

std::array<vk::VertexInputBindingDescription, 1> Model3D::Vertex::bindingDescriptions() noexcept {
	std::array<vk::VertexInputBindingDescription, 1> descriptions{ { 
		{ .binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex }
	} };

	return descriptions;
}
std::array<vk::VertexInputAttributeDescription, 3> Model3D::Vertex::attributeDescriptions() noexcept {
	std::array<vk::VertexInputAttributeDescription, 3> descriptions = { { 
		{ .location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, position) },
		{ .location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, normal) },
		{ .location = 2, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(Vertex, uv) }
	} };

	return descriptions;
}

Model3D::Model3D(Model3D&& other) noexcept 
	: vertexCount(std::exchange(other.vertexCount, 0)),
	  vertexMemory(std::move(other.vertexMemory)),
	  vertexBuffer(std::move(other.vertexBuffer)),
	  indexCount(std::exchange(other.indexCount, 0)),
	  indexMemory(std::move(other.indexMemory)),
	  indexBuffer(std::move(other.indexBuffer)),
	  _textureIndecies(std::exchange(other._textureIndecies, {})) {
}

Model3D& Model3D::operator=(Model3D&& other) noexcept {
	if (this == &other)
		return *this;
	
	destroy();
	vertexCount = std::exchange(other.vertexCount, 0);
	vertexBuffer = std::move(other.vertexBuffer);
	vertexMemory = std::move(other.vertexMemory);
	indexCount = std::exchange(other.indexCount, 0);
	indexBuffer = std::move(other.indexBuffer);
	indexMemory = std::move(other.indexMemory);
	_textureIndecies = std::exchange(other._textureIndecies, {});
	return *this;
}

Model3D::Model3D(std::filesystem::path file, vk::CommandPool commandPool)
	: vertexCount(0)
	, vertexMemory(nullptr)
	, vertexBuffer(nullptr)
	, indexCount(0)
	, indexMemory(nullptr)
	, indexBuffer(nullptr) {
	assert(std::filesystem::is_regular_file(file) && "File must exist to create model");
	assert(file.extension() == ".obj" && "Only obj files are supported for models");
	std::vector<RawVertex> rawVerticies = getRawVertices(file, commandPool);
	auto [indecies, vertices] = getIndecies(rawVerticies);
	
	createBuffers(indecies, vertices, commandPool);
}

Model3D::~Model3D() noexcept {
	destroy();
}

void Model3D::destroy() noexcept {
	if (*vertexBuffer == nullptr) {
		assert(*vertexMemory == nullptr && "A vertex buffer must have memory to have buffer");
		assert(*indexBuffer == nullptr && "If vertex buffer is null, index buffer must also be null");
		assert(*indexMemory == nullptr && "An index buffer must have memory to have buffer");
		assert(_textureIndecies.size() == 0 && "If vertex buffer is null, the model must not have any textures");
		return;
	}
	assert(vertexCount != 0 && "A vertex buffer must contain vertices");
	assert(*vertexMemory && "A vertex buffer must have memory to have buffer");
	assert(indexCount != 0 && "An index buffer must contain indices");
	assert(*indexBuffer && "An index buffer must have memory to have buffer");
	assert(_textureIndecies.size() > 0 && "Model must have at least one texture, even if it is just the default one");

	vertexBuffer.clear();
	vertexMemory.clear();
	indexBuffer.clear();
	indexMemory.clear();
	for (auto& i : _textureIndecies) {
		TextureCache::unloadTexture(i.second);
	}
}

std::vector<Model3D::RawVertex> Model3D::getRawVertices(const std::filesystem::path& file, vk::CommandPool commandPool) const {
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, file.string().c_str(), file.parent_path().string().c_str())) {
		throw std::runtime_error(warn + err);
	}
	if (!warn.empty())
		Console::log(Console::Log::Type::warning, "Tiny obj loader: " + warn);

	std::vector<RawVertex> vertices;
	size_t vertexCount = 0;

	for (const auto& shape : shapes) {
		vertexCount += shape.mesh.indices.size();
	}

	vertices.reserve(vertexCount);

	TextureCache::ID textureID = 0;
	TextureCache::ID lastTextureID = 0;
	int lastMaterialIndex = -1;
	std::set<size_t> indexSet = {};
	for (const auto& shape : shapes) {
		const auto& indices = shape.mesh.indices;

		for (size_t i = 0; i < indices.size(); i += 3) {
			RawVertex v[3]{};
			
			const size_t f = i / 3;


			const int materialIndex = shape.mesh.material_ids[f];
			if (materialIndex != lastMaterialIndex) {
				lastMaterialIndex = materialIndex;
				if (materialIndex >= 0) {
					const auto& material = materials[materialIndex];

					if (material.name != "__no_material__") {
						glm::vec3 color{ material.diffuse[0], material.diffuse[1], material.diffuse[2] };
						try {
							if (material.diffuse_texname.empty()) {
								textureID = TextureCache::loadTexture(color, "", commandPool);
							}
							else {
								textureID = TextureCache::loadTexture(color,
									file.parent_path() / std::filesystem::path(material.diffuse_texname), commandPool);
							}
							// unload textures that have different name but are the same
							if (textureID == lastTextureID) {
								TextureCache::unloadTexture(textureID);
							}
							lastTextureID = textureID;
						}
						catch (std::exception& e) {
							Console::log(Console::Log::Type::warning, std::string("Tried loading texture for model, errored with: ") + e.what());
							textureID = 0;
						}
					}
				}
			}

			for (int k = 0; k < 3; ++k) {
				const auto& index = indices[i + k];

				v[k].textureID = textureID;

				v[k].position = {
					attrib.vertices[3 * index.vertex_index + 0],
					attrib.vertices[3 * index.vertex_index + 1],
					attrib.vertices[3 * index.vertex_index + 2],
				};

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

			// cant trust obj file normale's
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
	return vertices;
}

std::pair<std::vector<uint32_t>, std::vector<Model3D::Vertex>> Model3D::getIndecies(const std::vector<RawVertex>& rawVertices) {
	assert(rawVertices.size() > 0 && "Model must have at least one vertex");
	assert(rawVertices.size() % 3 == 0 && "Raw vertices must be a multiple of 3 since they are triangles");
	std::vector<Vertex> vertices{};
	std::vector<uint32_t> indecies{};
	indecies.reserve(rawVertices.size());

	_textureIndecies.emplace_back(0, rawVertices[0].textureID);

	uint32_t vertexCount = 0;
	uint32_t i = 0;
	std::unordered_map<RawVertex, uint32_t, RawVertexHash> vertexToIndex{};
	for (auto& rawVertex : rawVertices) {
		if (vertexToIndex.contains(rawVertex)) {
			indecies.push_back(vertexToIndex.at(rawVertex));
			if (rawVertex.textureID != _textureIndecies.back().second) {
				_textureIndecies.emplace_back(i, rawVertex.textureID);
			}
			i++;
			continue;
		}
		vertices.emplace_back(Vertex{
			.position = rawVertex.position,
			.normal = rawVertex.normal,
			.uv = rawVertex.uv
		});
		if (rawVertex.textureID != _textureIndecies.back().second) {
			_textureIndecies.emplace_back(i, rawVertex.textureID);
		}
		vertexToIndex[rawVertex] = vertexCount;
		indecies.push_back(vertexCount);
		vertexCount++;
		i++;
	}

	return std::make_pair(std::move(indecies), std::move(vertices));
}

void Model3D::createBuffers(const std::vector<uint32_t>& indecies, const std::vector<Vertex>& vertices, vk::CommandPool commandPool) {
	assert(vertices.size() > 0 && "Model must have at least one vertex");
	assert(indecies.size() > 0 && "Model must have at least one index");
	vertexCount = vertices.size();
	indexCount = indecies.size();
	{
		Buffer stagingBuffer(
			sizeof(vertices[0]),
			static_cast<uint32_t>(vertices.size()),
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		stagingBuffer.map();
		stagingBuffer.writeToBuffer((void*)vertices.data());
		stagingBuffer.unmap();

		auto [newBuffer, newMemory] = createGPUBuffer(stagingBuffer, vk::BufferUsageFlagBits::eVertexBuffer, commandPool);
		vertexBuffer = std::move(newBuffer);
		vertexMemory = std::move(newMemory);
	}
	{
		Buffer stagingBuffer(
			sizeof(indecies[0]),
			static_cast<uint32_t>(indecies.size()),
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		stagingBuffer.map();
		stagingBuffer.writeToBuffer((void*)indecies.data());
		stagingBuffer.unmap();
		auto [newBuffer, newMemory] = createGPUBuffer(stagingBuffer, vk::BufferUsageFlagBits::eIndexBuffer, commandPool);
		indexBuffer = std::move(newBuffer);
		indexMemory = std::move(newMemory);
	}
}

void Model3D::copyBuffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size, vk::CommandPool commandPool) {
	assert(src != nullptr && dst != nullptr && "Buffers must be valid to copy");
	assert(size > 0 && "Size must be greater than 0 to copy");
	auto comandBuffer = beginSingleTimeCommands(commandPool);

	vk::BufferCopy copyRegion{
		.srcOffset = 0,
		.dstOffset = 0,
		.size = size
	};
	comandBuffer.copyBuffer(src, dst, copyRegion);
	endSingleTimeCommands(comandBuffer);
}

std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> Model3D::createGPUBuffer(
	Buffer& buffer, vk::BufferUsageFlags usage, vk::CommandPool commandPool) {
	assert(buffer.getBuffer() != nullptr && "Buffer must be valid to create GPU buffer");
	assert(usage && "Buffer usage must be specified");

	vk::DeviceSize bufferSize = buffer.getBufferSize();

	vk::BufferCreateInfo bufferInfo{
		.size = bufferSize,
		.usage = usage | vk::BufferUsageFlagBits::eTransferDst,
		.sharingMode = vk::SharingMode::eExclusive
	};

	vk::raii::Buffer newBuffer = App::device.createBuffer(bufferInfo);

	vk::MemoryRequirements memory_requirements = newBuffer.getMemoryRequirements();

	vk::PhysicalDeviceMemoryProperties mem_properties;
	mem_properties = App::physicalDevice.getMemoryProperties();

	uint32_t memoryIndex = (uint32_t)-1;

	for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
		if (!(memory_requirements.memoryTypeBits & (1 << i)))
			continue;
		if ((mem_properties.memoryTypes[i].propertyFlags &
				(vk::MemoryPropertyFlagBits::eDeviceLocal)) ==
			(vk::MemoryPropertyFlagBits::eDeviceLocal)) {
			memoryIndex = i;
			break;
		}
	}
	if (memoryIndex == (uint32_t)-1)
		throw std::runtime_error("Failed to find suitable memory type");


	vk::MemoryAllocateInfo alloc_info{
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = memoryIndex
	};

	vk::raii::DeviceMemory newMemory = App::device.allocateMemory(alloc_info);

	newBuffer.bindMemory(*newMemory, 0);

	copyBuffer(buffer.getBuffer(), *newBuffer, bufferSize, commandPool);

	return std::make_pair(std::move(newBuffer), std::move(newMemory));
}

void Model3D::draw(vk::CommandBuffer cmd, vk::PipelineLayout layout) const noexcept {
	assert(*vertexBuffer && *indexBuffer && "Model3D must have vertex and index buffer to draw");
	vk::Buffer buffers[] = { *vertexBuffer };
	vk::DeviceSize offsets[] = { 0 };
	cmd.bindVertexBuffers(0, 1, buffers, offsets);
	cmd.bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint32);

	for (size_t i = 0; i < _textureIndecies.size(); ++i) {
		uint32_t start = _textureIndecies[i].first;

		uint32_t end =
			(i + 1 < _textureIndecies.size())
				? _textureIndecies[i + 1].first
				: indexCount;

		TextureCache::ID textureID =
			_textureIndecies[i].second;

		TextureCache::getTexture(textureID).bind(cmd, layout);

		cmd.drawIndexed(end - start, 1, start, 0, 0);
	}
}

Model3D& ModelCache::getModel(ID id) noexcept {
	assert(_cache.contains(id) && "ModelCache does not contain model with given id");
	return _cache[id];
}

ID ModelCache::loadModel(std::filesystem::path file, vk::CommandPool commandPool) {
	static std::atomic<ID> currID = 1;
	file = std::filesystem::weakly_canonical(file);

	ID id;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_idMap.contains(file)) {
			id = _idMap.at(file);
			_refCounts[id]++;
			return id;
		}
		id = currID.fetch_add(1);
		_idMap[file] = id;
		_refCounts[id] = 1;
	}
	
	// the model creation part is the important one
	Model3D model;
	try {
		model = Model3D(file, commandPool);
	}
	catch(std::exception& e) {
		Console::log(Console::Log::Type::error, std::string("Failed to create model: ") + e.what());
		std::lock_guard<std::mutex> lock(_mutex);
		_idMap.erase(file);
		_refCounts.erase(id);
		throw std::runtime_error("Failed to create model.");
	}

	std::lock_guard<std::mutex> lock(_mutex);
	_cache[id] = std::move(model);

	return id;
}

void ModelCache::unloadModel(ID id) {
	assert(id != 0 && "ID 0 is reserved for no model");
	_refCounts[id]--;
	if (_refCounts[id] != 0) {
		return;
	}
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
