#include "Buffer.hpp"

#include <cassert>
#include <cstring>

#include "VulkanApp.hpp"

using namespace vulkan;

static void vkCheck(vk::Result err) {
	if (err == vk::Result::eSuccess)
		return;
	fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
	if (err < vk::Result::eSuccess)
		abort();
}

vk::DeviceSize Buffer::getAlignment(vk::DeviceSize instanceSize, vk::DeviceSize minOffsetAlignment) {
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

	vkCheck(App::device.createBuffer(&vertexBufferInfo, nullptr, &_buffer));

	vk::MemoryRequirements memory_requirements;
	App::device.getBufferMemoryRequirements(_buffer, &memory_requirements);

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
	vkCheck(App::device.allocateMemory(&alloc_info, nullptr, &_memory));

	App::device.bindBufferMemory(_buffer, _memory, 0);
}

Buffer::~Buffer() {
	unmap();
	App::device.destroyBuffer(_buffer);
	App::device.freeMemory(_memory);
}

vk::Result Buffer::map(vk::DeviceSize size, vk::DeviceSize offset) {
	assert(_buffer && _memory && "Called map on buffer before create");
	return App::device.mapMemory(_memory, offset, size, vk::MemoryMapFlags(), &_mapped);
}

void Buffer::unmap() {
	if (_mapped) {
		App::device.unmapMemory(_memory);
		_mapped = nullptr;
	}
}

void Buffer::writeToBuffer(void* data, vk::DeviceSize size, vk::DeviceSize offset) {
	assert(_mapped && "Cannot copy to unmapped buffer");

	if (size == VK_WHOLE_SIZE) {
		memcpy(_mapped, data, _bufferSize);
	}
	else {
		char* memOffset = (char*)_mapped;
		memOffset += offset;
		memcpy(memOffset, data, size);
	}
}

vk::Result Buffer::flush(vk::DeviceSize size, vk::DeviceSize offset) {
	vk::MappedMemoryRange mappedRange = {};
	mappedRange.memory = _memory;
	mappedRange.offset = offset;
	mappedRange.size = size;
	return App::device.flushMappedMemoryRanges(1, &mappedRange);
}

vk::Result Buffer::invalidate(vk::DeviceSize size, vk::DeviceSize offset) {
	vk::MappedMemoryRange mappedRange = {};
	mappedRange.sType = vk::StructureType::eMappedMemoryRange;
	mappedRange.memory = _memory;
	mappedRange.offset = offset;
	mappedRange.size = size;
	return App::device.invalidateMappedMemoryRanges(1, &mappedRange);
}

vk::DescriptorBufferInfo Buffer::descriptorInfo(vk::DeviceSize size, vk::DeviceSize offset) {
	return vk::DescriptorBufferInfo{
		_buffer,
		offset,
		size,
	};
}

void Buffer::writeToIndex(void* data, int index) {
	writeToBuffer(data, _instanceSize, index * _alignmentSize);
}

vk::Result Buffer::flushIndex(int index) { return flush(_alignmentSize, index * _alignmentSize); }

vk::DescriptorBufferInfo Buffer::descriptorInfoForIndex(int index) {
	return descriptorInfo(_alignmentSize, index * _alignmentSize);
}

vk::Result Buffer::invalidateIndex(int index) {
	return invalidate(_alignmentSize, index * _alignmentSize);
}