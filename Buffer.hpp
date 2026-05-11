#pragma once

#include "vulkan/vulkan.hpp"

namespace vulkan {

class Buffer {
public:
	Buffer(
		vk::DeviceSize instanceSize,
		uint32_t instanceCount,
		vk::BufferUsageFlags usageFlags,
		vk::MemoryPropertyFlags memoryPropertyFlags,
		vk::DeviceSize minOffsetAlignment = 1);
	~Buffer();

	Buffer(const Buffer&) = delete;
	Buffer& operator=(const Buffer&) = delete;

	vk::Result map(vk::DeviceSize size = VK_WHOLE_SIZE, vk::DeviceSize offset = 0);
	void unmap();

	void writeToBuffer(void* data, vk::DeviceSize size = VK_WHOLE_SIZE, vk::DeviceSize offset = 0);
	vk::Result flush(vk::DeviceSize size = VK_WHOLE_SIZE, vk::DeviceSize offset = 0);
	vk::DescriptorBufferInfo descriptorInfo(vk::DeviceSize size = VK_WHOLE_SIZE, vk::DeviceSize offset = 0);
	vk::Result invalidate(vk::DeviceSize size = VK_WHOLE_SIZE, vk::DeviceSize offset = 0);

	void writeToIndex(void* data, int index);
	vk::Result flushIndex(int index);
	vk::DescriptorBufferInfo descriptorInfoForIndex(int index);
	vk::Result invalidateIndex(int index);

	/*getters*/
	vk::Buffer getBuffer() const { return _buffer; }
	void* getMappedMemory() const { return _mapped; }
	uint32_t getInstanceCount() const { return _instanceCount; }
	vk::DeviceSize getInstanceSize() const { return _instanceSize; }
	vk::DeviceSize getAlignmentSize() const { return _instanceSize; }
	vk::BufferUsageFlags getUsageFlags() const { return _usageFlags; }
	vk::MemoryPropertyFlags getMemoryPropertyFlags() const { return _memoryPropertyFlags; }
	vk::DeviceSize getBufferSize() const { return _bufferSize; }

private:
	static vk::DeviceSize getAlignment(vk::DeviceSize instanceSize, vk::DeviceSize minOffsetAlignment);

private:
	void* _mapped = nullptr;
	vk::Buffer _buffer = nullptr;
	vk::DeviceMemory _memory = nullptr;

	vk::DeviceSize _bufferSize = 0;
	uint32_t _instanceCount = 0;
	vk::DeviceSize _instanceSize = 0;
	vk::DeviceSize _alignmentSize = 0;
	vk::BufferUsageFlags _usageFlags = vk::BufferUsageFlagBits(0);
	vk::MemoryPropertyFlags _memoryPropertyFlags = vk::MemoryPropertyFlagBits(0);
};

}