#include "Buffer.hpp"

#include <cassert>
#include <cstring>

#include "VulkanApp.hpp"

using namespace renderer;

vk::DeviceSize Buffer::getAlignment(vk::DeviceSize instanceSize, vk::DeviceSize minOffsetAlignment) noexcept {
	if (minOffsetAlignment > 0) {
		return (instanceSize + minOffsetAlignment - 1) & ~(minOffsetAlignment - 1);
	}
	return instanceSize;
}

Buffer::Buffer(
	vk::DeviceSize instanceSize,
	uint32_t instanceCount,
	vk::BufferUsageFlags usageFlags,
	vk::MemoryPropertyFlags memoryPropertyFlags,
	vk::DeviceSize minOffsetAlignment)
	: _instanceSize{ instanceSize },
	  _instanceCount{ instanceCount },
	  _usageFlags{ usageFlags },
	  _memoryPropertyFlags{ memoryPropertyFlags } 
{
	_alignmentSize = getAlignment(instanceSize, minOffsetAlignment);
	_bufferSize = _alignmentSize * instanceCount;

	vk::BufferCreateInfo vertexBufferInfo{
		.size = _bufferSize,
		.usage = _usageFlags,
		.sharingMode = vk::SharingMode::eExclusive
	};

	_buffer = App::device.createBuffer(vertexBufferInfo);

	vk::MemoryRequirements memory_requirements = _buffer.getMemoryRequirements();

	vk::PhysicalDeviceMemoryProperties mem_properties;
	mem_properties = App::physicalDevice.getMemoryProperties();

	uint32_t memoryIndex = 0;
	bool foundMemoryIndex = false;

	for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
		if (memory_requirements.memoryTypeBits & (1 << i)) {
			if ((mem_properties.memoryTypes[i].propertyFlags & memoryPropertyFlags) == memoryPropertyFlags) {
				memoryIndex = i;
				foundMemoryIndex = true;
				break;
			}
		}
	}
	if (!foundMemoryIndex)
		throw std::runtime_error("Failed to find suitable memory type");


	vk::MemoryAllocateInfo alloc_info{
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = memoryIndex
	};
	_memory = App::device.allocateMemory(alloc_info);

	_buffer.bindMemory(*_memory, 0);
}

Buffer::~Buffer() noexcept {
	unmap();
}

Buffer::Buffer(Buffer&& other) noexcept
	: _mapped(std::exchange(other._mapped, nullptr))
	, _memory(std::move(other._memory))
	, _buffer(std::move(other._buffer))
	, _bufferSize(std::exchange(other._bufferSize, 0))
	, _instanceCount(std::exchange(other._instanceCount, 0))
	, _instanceSize(std::exchange(other._instanceSize, 0))
	, _alignmentSize(std::exchange(other._alignmentSize, 0))
	, _usageFlags(std::exchange(other._usageFlags, vk::BufferUsageFlags{}))
	, _memoryPropertyFlags(std::exchange(other._memoryPropertyFlags, vk::MemoryPropertyFlags{})) {
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
	if (this == &other) {
		return *this;
	}

	unmap();

	_mapped = std::exchange(other._mapped, nullptr);
	_buffer = std::move(other._buffer);
	_memory = std::move(other._memory);

	_bufferSize = std::exchange(other._bufferSize, 0);
	_instanceCount = std::exchange(other._instanceCount, 0);
	_instanceSize = std::exchange(other._instanceSize, 0);
	_alignmentSize = std::exchange(other._alignmentSize, 0);

	_usageFlags = std::exchange(other._usageFlags, vk::BufferUsageFlags{});
	_memoryPropertyFlags = std::exchange(other._memoryPropertyFlags, vk::MemoryPropertyFlags{});

	return *this;
}

void Buffer::map(vk::DeviceSize size, vk::DeviceSize offset) {
	assert(*_buffer && *_memory && "Called map on buffer before create");
	_mapped = _memory.mapMemory(offset, size);
}

void Buffer::unmap() noexcept {
	if (_mapped) {
		_memory.unmapMemory();
		_mapped = nullptr;
	}
}

void Buffer::writeToBuffer(const void* data, const vk::DeviceSize size, const vk::DeviceSize offset) noexcept {
	assert(_mapped && "Cannot copy to unmapped buffer");
	assert(data && "Can't copy a nullptr");

	if (size == VK_WHOLE_SIZE) {
		std::memcpy(_mapped, data, _bufferSize);
	}
	else {
		char* memOffset = (char*)_mapped;
		memOffset += offset;
		std::memcpy(memOffset, data, size);
	}
}

void Buffer::flush(vk::DeviceSize size, vk::DeviceSize offset) {
	vk::MappedMemoryRange mappedRange = {
	.memory = *_memory,
	.offset = offset,
	.size = size
	};
	App::device.flushMappedMemoryRanges(mappedRange);
}

void Buffer::invalidate(vk::DeviceSize size, vk::DeviceSize offset) {
	vk::MappedMemoryRange mappedRange = {
	.memory = *_memory,
	.offset = offset,
	.size = size,
	};
	App::device.invalidateMappedMemoryRanges(mappedRange);
}

vk::DescriptorBufferInfo Buffer::descriptorInfo(vk::DeviceSize size, vk::DeviceSize offset) noexcept {
	return vk::DescriptorBufferInfo{
		_buffer,
		offset,
		size,
	};
}

void Buffer::writeToIndex(void* data, vk::DeviceSize index) noexcept {
	writeToBuffer(data, _instanceSize, index * _alignmentSize);
}

void Buffer::flushIndex(vk::DeviceSize index) { flush(_alignmentSize, index * _alignmentSize); }

vk::DescriptorBufferInfo Buffer::descriptorInfoForIndex(vk::DeviceSize index) noexcept {
	return descriptorInfo(_alignmentSize, index * _alignmentSize);
}

void Buffer::invalidateIndex(vk::DeviceSize index) {
	invalidate(_alignmentSize, index * _alignmentSize);
}
