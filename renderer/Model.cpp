#include "Model.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include "Renderer.hpp"
#include "helpers.hpp"
#include "Runtime.hpp"

#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <iostream>
#include <limits>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

// TODO: create settings
// render distance, unload distance, mipmaps, mipmap distance

namespace renderer {

namespace {

Hash hashModelData(const Model3D::Data& data) {
	Hash::Builder hash;
	// Hashing the size must be first to avoid collisions with models where the
	// end of vertex data may be the same as the texture index data of another model
	hash.update(static_cast<uint64_t>(data.vertices.size()));
	hash.update(static_cast<uint64_t>(data.textureIndices.size()));
	for (const Model3D::Vertex& vertex : data.vertices) {
		hash.update(vertex.position.x);
		hash.update(vertex.position.y);
		hash.update(vertex.position.z);
		hash.update(vertex.normal.x);
		hash.update(vertex.normal.y);
		hash.update(vertex.normal.z);
		hash.update(vertex.uv.x);
		hash.update(vertex.uv.y);
	}
	for (const Model3D::TextureIndex& textureIndex : data.textureIndices) {
		hash.update(textureIndex.firstIndex);
		hash.update(static_cast<uint64_t>(textureIndex.textureID));
	}
	return hash.finish();
}

void releaseTextureReferences(std::vector<Model3D::TextureIndex>& textureIndices) noexcept {
	for (const Model3D::TextureIndex& textureIndex : textureIndices) {
		TextureCache::unloadTexture(textureIndex.textureID);
	}
	textureIndices.clear();
}

}

Model3D::Data::~Data() noexcept {
	releaseTextureReferences(textureIndices);
}

Model3D::Data::Data(Data&& other) noexcept
	: vertices(std::move(other.vertices))
	, textureIndices(std::exchange(other.textureIndices, {})) {}

Model3D::Data& Model3D::Data::operator=(Data&& other) noexcept {
	if (this == &other) {
		return *this;
	}
	releaseTextureReferences(textureIndices);
	vertices = std::move(other.vertices);
	textureIndices = std::exchange(other.textureIndices, {});
	return *this;
}

std::array<vk::VertexInputBindingDescription, 1> Model3D::Vertex::bindingDescriptions() noexcept {
	std::array<vk::VertexInputBindingDescription, 1> descriptions{ { { .binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex } } };

	return descriptions;
}
std::array<vk::VertexInputAttributeDescription, 3> Model3D::Vertex::attributeDescriptions() noexcept {
	std::array<vk::VertexInputAttributeDescription, 3> descriptions = { { { .location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, position) },
		{ .location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, normal) },
		{ .location = 2, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(Vertex, uv) } } };

	return descriptions;
}

Model3D::Model3D(Model3D&& other) noexcept
	: vertexCount(std::exchange(other.vertexCount, 0))
	, vertexMemory(std::move(other.vertexMemory))
	, vertexBuffer(std::move(other.vertexBuffer))
	, _textureIndices(std::exchange(other._textureIndices, {}))
	, _boundingSphere(std::exchange(other._boundingSphere, {}))
	, indexCount(std::exchange(other.indexCount, 0))
	, indexMemory(std::move(other.indexMemory))
	, indexBuffer(std::move(other.indexBuffer)) {
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
	_textureIndices = std::exchange(other._textureIndices, {});
	_boundingSphere = std::exchange(other._boundingSphere, {});
	return *this;
}

Model3D::Model3D(Data&& data, vk::CommandPool commandPool)
	: Model3D() {
	assert(!data.vertices.empty() && "Model vertices must not be empty");
	assert(data.vertices.size() % 3 == 0 && "Model vertices must be a multiple of 3");
	if (data.textureIndices.empty()) {
		data.textureIndices.emplace_back(TextureIndex{});
	}
	assert(data.textureIndices.front().firstIndex == 0 && "The first texture must start at index 0");
	assert(std::ranges::all_of(data.textureIndices, [&](const TextureIndex& textureIndex) {
		return textureIndex.firstIndex < data.vertices.size();
	}) && "All textures must refer to a valid model index");	
	assert(std::ranges::none_of(data.textureIndices, [](const TextureIndex& textureIndex) {
		return textureIndex.firstIndex % 3 != 0;
	}) && "All textures must start at a triangle boundary");
	assert(std::ranges::is_sorted(data.textureIndices, [](const TextureIndex& a, const TextureIndex& b) {
		return a.firstIndex < b.firstIndex;
	}) && "Texture indices must be sorted by first index");

	_textureIndices = std::exchange(data.textureIndices, {});
	try {
		const auto [indices, vertices] = getIndices(data.vertices);
		calculateBoundingSphere(vertices);
		createBuffers(indices, vertices, commandPool);
	}
	catch (...) {
		releaseTextureReferences(_textureIndices);
		throw;
	}
}

bool Model3D::isSupportedFile(const std::filesystem::path& file) {
	assert(std::filesystem::is_regular_file(file) && "File must be a regular file");
	return isSupportedFileFormat(file.extension().string());
}

bool Model3D::isSupportedFileFormat(std::string fileExtension) {
	if (fileExtension.empty())
		return false;
	std::ranges::transform(fileExtension, fileExtension.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	if (fileExtension[0] == '.')
		fileExtension.erase(0, 1);
	return fileExtension == "obj";
}

Model3D::~Model3D() noexcept {
	destroy();
}

void Model3D::destroy() noexcept {
	if (*vertexBuffer == nullptr) {
		assert(*vertexMemory == nullptr && "A vertex buffer must have memory to have buffer");
		assert(*indexBuffer == nullptr && "If vertex buffer is null, index buffer must also be null");
		assert(*indexMemory == nullptr && "An index buffer must have memory to have buffer");
		assert(_textureIndices.empty() && "If vertex buffer is null, the model must not have any textures");
		return;
	}
	assert(vertexCount != 0 && "A vertex buffer must contain vertices");
	assert(*vertexMemory && "A vertex buffer must have memory to have buffer");
	assert(indexCount != 0 && "An index buffer must contain indices");
	assert(*indexBuffer && "An index buffer must have memory to have buffer");
	assert(!_textureIndices.empty() && "Model must have at least one texture, even if it is just the default one");

	vertexBuffer.clear();
	vertexMemory.clear();
	indexBuffer.clear();
	indexMemory.clear();
	releaseTextureReferences(_textureIndices);
}

Model3D::Data Model3D::loadFile(const std::filesystem::path& file, vk::CommandPool commandPool) {
	assert(isSupportedFile(file) && "File format is not supported");
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, file.string().c_str(), file.parent_path().string().c_str())) {
		throw std::runtime_error(warn + err);
	}
	if (!warn.empty())
		Runtime::log(LogLevel::warning, "Tiny obj loader: " + warn);

	Data data;
	size_t vertexCount = 0;

	for (const auto& shape : shapes) {
		vertexCount += shape.mesh.indices.size();
	}

	data.vertices.reserve(vertexCount);
	data.textureIndices.reserve(materials.size());

	TextureCache::ID textureID = 0;
	int lastMaterialIndex = std::numeric_limits<int>::min();
	try {
		for (const auto& shape : shapes) {
			const auto& indices = shape.mesh.indices;

			for (size_t i = 0; i < indices.size(); i += 3) {
				Vertex triangle[3]{};
				const size_t face = i / 3;
				const int materialIndex = shape.mesh.material_ids[face];
				assert(materialIndex >= -1 && materialIndex < static_cast<int>(materials.size()) && "Material index must be valid");

				if (materialIndex != lastMaterialIndex) {
					lastMaterialIndex = materialIndex;
					TextureCache::ID nextTextureID = 0;
					if (materialIndex >= 0) {
						const auto& material = materials[materialIndex];
						const glm::vec3 color{ material.diffuse[0], material.diffuse[1], material.diffuse[2] };
						try {
							const std::filesystem::path textureFile = file.parent_path() / material.diffuse_texname;
							if (material.diffuse_texname.empty() || !std::filesystem::is_regular_file(textureFile)) {
								nextTextureID = TextureCache::loadTexture(color, "", commandPool);
							}
							else {
								nextTextureID = TextureCache::loadTexture(color, textureFile, commandPool);
							}
						}
						catch (const std::exception& e) {
							Runtime::log(LogLevel::warning, std::string("Tried loading texture for model, errored with: ") + e.what());
						}
					}

					if (data.textureIndices.empty() || nextTextureID != textureID) {
						textureID = nextTextureID;
						// reserved space but a material may be used multiple times
						try {
							data.textureIndices.emplace_back(TextureIndex{
								.firstIndex = static_cast<uint32_t>(data.vertices.size()),
								.textureID = textureID });
							}
						catch (const std::exception& e) {
							if (textureID != 0) {
								TextureCache::unloadTexture(textureID);
								if (data.textureIndices.empty())
									textureID = 0;
								else
									textureID = data.textureIndices.back().textureID;
							}
							Runtime::log(LogLevel::warning, std::string("Failed to emplace texture index, continuing using prior texture. Failed with: ") + e.what());
						}
					}
					else if (textureID != 0) {
						TextureCache::unloadTexture(nextTextureID);
					}
				}

				for (int k = 0; k < 3; ++k) {
					const auto& index = indices[i + k];
					triangle[k].position = {
						attrib.vertices[3 * index.vertex_index + 0],
						attrib.vertices[3 * index.vertex_index + 1],
						attrib.vertices[3 * index.vertex_index + 2],
					};

					if (!attrib.texcoords.empty() && index.texcoord_index >= 0) {
						triangle[k].uv = {
							attrib.texcoords[2 * index.texcoord_index + 0],
							1.0f - attrib.texcoords[2 * index.texcoord_index + 1],
						};
					}

					if (!attrib.normals.empty() && index.normal_index >= 0) {
						triangle[k].normal = {
							attrib.normals[3 * index.normal_index + 0],
							attrib.normals[3 * index.normal_index + 1],
							attrib.normals[3 * index.normal_index + 2],
						};
					}
				}
				if (attrib.normals.empty()) {
					const glm::vec3 edge1 = triangle[1].position - triangle[0].position;
					const glm::vec3 edge2 = triangle[2].position - triangle[0].position;
					const glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));
					triangle[0].normal = normal;
					triangle[1].normal = normal;
					triangle[2].normal = normal;
				}

				data.vertices.push_back(triangle[0]);
				data.vertices.push_back(triangle[1]);
				data.vertices.push_back(triangle[2]);
			}
		}
	}
	catch (...) {
		releaseTextureReferences(data.textureIndices);
		throw;
	}
	assert(data.vertices.size() == data.vertices.capacity() && "Vertex count must match reserved size");
	return data;
}

std::pair<std::vector<uint32_t>, std::vector<Model3D::Vertex>> Model3D::getIndices(const std::vector<Vertex>& rawVertices) const {
	std::vector<Vertex> vertices{};
	std::vector<uint32_t> indices{};
	indices.reserve(rawVertices.size());

	uint32_t vertexCount = 0;
	std::unordered_map<Vertex, uint32_t, VertexHash> vertexToIndex{};
	for (const Vertex& rawVertex : rawVertices) {
		if (vertexToIndex.contains(rawVertex)) {
			indices.push_back(vertexToIndex.at(rawVertex));
			continue;
		}
		vertices.push_back(rawVertex);
		vertexToIndex[rawVertex] = vertexCount;
		indices.push_back(vertexCount++);
	}

	return std::make_pair(std::move(indices), std::move(vertices));
}

void Model3D::calculateBoundingSphere(const std::vector<Vertex>& vertices) noexcept {
	assert(!vertices.empty() && "Model must have vertices to calculate bounds");
	glm::vec3 minimum = vertices.front().position;
	glm::vec3 maximum = vertices.front().position;
	for (const Vertex& vertex : vertices) {
		minimum = glm::min(minimum, vertex.position);
		maximum = glm::max(maximum, vertex.position);
	}

	_boundingSphere.center = (minimum + maximum) * 0.5f;
	_boundingSphere.radius = 0.0f;
	for (const Vertex& vertex : vertices) {
		_boundingSphere.radius = std::max(
			_boundingSphere.radius,
			glm::length(vertex.position - _boundingSphere.center));
	}
}

void Model3D::createBuffers(const std::vector<uint32_t>& indices, const std::vector<Vertex>& vertices, vk::CommandPool commandPool) {
	assert(vertices.size() > 0 && "Model must have at least one vertex");
	assert(indices.size() > 0 && "Model must have at least one index");
	vertexCount = vertices.size();
	indexCount = indices.size();
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
			sizeof(indices[0]),
			static_cast<uint32_t>(indices.size()),
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		stagingBuffer.map();
		stagingBuffer.writeToBuffer((void*)indices.data());
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

	for (size_t i = 0; i < _textureIndices.size(); ++i) {
		uint32_t start = _textureIndices[i].firstIndex;

		uint32_t end =
			(i + 1 < _textureIndices.size())
				? _textureIndices[i + 1].firstIndex
				: indexCount;

		TextureCache::ID textureID =
			_textureIndices[i].textureID;

		TextureCache::getTexture(textureID).bind(cmd, layout);

		cmd.drawIndexed(end - start, 1, start, 0, 0);
	}
}

void Model3D::drawGeometry(vk::CommandBuffer cmd) const noexcept {
	assert(*vertexBuffer && *indexBuffer && "Model3D must have vertex and index buffer to draw");
	const vk::Buffer buffer = *vertexBuffer;
	constexpr vk::DeviceSize offset = 0;
	cmd.bindVertexBuffers(0, 1, &buffer, &offset);
	cmd.bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint32);
	cmd.drawIndexed(indexCount, 1, 0, 0, 0);
}

const std::vector<Model3D::TextureIndex>& Model3D::getTextures() const noexcept {
	return _textureIndices;
}

Model3D& ModelCache::getModel(ID id) noexcept {
	assert(_cache.contains(id) && "ModelCache does not contain model with given id");
	return _cache.find(id)->second;
}

size_t ModelCache::getSize() noexcept {
	return _idMap.size();
}

ModelCache::ID ModelCache::loadModel(Model3D::Data&& data, vk::CommandPool commandPool) {
	static std::atomic<ID> currID = 1;
	const Hash hash = hashModelData(data);

	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (const auto idEntry = _idMap.find(hash); idEntry != _idMap.end()) {
			const ID id = idEntry->second;
			assert(_refCounts.contains(id) && "Every model ID-map entry must have a reference count");
			++_refCounts.find(id)->second;
			releaseTextureReferences(data.textureIndices);
			return id;
		}
	}

	Model3D model;
	try {
		model = Model3D(std::move(data), commandPool);
	}
	catch (const std::exception& e) {
		Runtime::log(LogLevel::error, std::string("Failed to create model: ") + e.what());
		throw std::runtime_error("Failed to create model.");
	}

	std::lock_guard<std::mutex> lock(_mutex);
	if (const auto idEntry = _idMap.find(hash); idEntry != _idMap.end()) {
		const ID id = idEntry->second;
		assert(_cache.contains(id) && "Every model ID-map entry must have a cached model");
		assert(_refCounts.contains(id) && "Every model ID-map entry must have a reference count");
		++_refCounts.find(id)->second;
		return id;
	}

	const ID id = currID.fetch_add(1);
	try {
		_cache.emplace(id, std::move(model));
		_refCounts.emplace(id, 1);
		_idMap.emplace(hash, id);
		return id;
	}
	catch (...) {
		_refCounts.erase(id);
		_cache.erase(id);
		throw;
	}
}

void ModelCache::addModelRefrence(ID id) {
	assert(id != 0 && "ID 0 is reserved for no model");
	std::lock_guard<std::mutex> lock(_mutex);
	assert(_cache.contains(id) && "ModelCache does not contain model with given id");
	assert(_refCounts.contains(id) && "ModelCache does not contain ref count for given id");
	assert(_refCounts.find(id)->second > 0 && "ModelCache ref count for given id is already 0");
	assert(std::ranges::any_of(_idMap, [id](const auto& entry) noexcept {
		return entry.second == id;
	}) && "Every cached model ID must have an ID-map entry");
	++_refCounts.find(id)->second;
}

void ModelCache::unloadModel(ID id) noexcept {
	assert(id != 0 && "ID 0 is reserved for no model");
	assert(_cache.contains(id) && "ModelCache does not contain model with given id");
	assert(_refCounts.contains(id) && "ModelCache does not contain ref count for given id");
	assert(_refCounts.find(id)->second > 0 && "ModelCache ref count for given id is already 0");

	const auto refCount = _refCounts.find(id);
	if (--refCount->second != 0) {
		return;
	}
	const auto idMapEntry = std::ranges::find_if(_idMap, [id](const auto& entry) noexcept {
		return entry.second == id;
	});
	_cache.erase(id);
	_refCounts.erase(refCount);
	_idMap.erase(idMapEntry);
}
}
