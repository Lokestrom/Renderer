#pragma once

#include "vulkan/vulkan_raii.hpp"

namespace renderer {

class Buffer {
public:
	Buffer() noexcept = default;
	Buffer(
		vk::DeviceSize instanceSize,
		uint32_t instanceCount,
		vk::BufferUsageFlags usageFlags,
		vk::MemoryPropertyFlags memoryPropertyFlags,
		vk::DeviceSize minOffsetAlignment = 1);
	~Buffer() noexcept;

	Buffer(const Buffer&) = delete;
	Buffer& operator=(const Buffer&) = delete;

	Buffer(Buffer&&) noexcept;
	Buffer& operator=(Buffer&&) noexcept;

	void map(vk::DeviceSize size = VK_WHOLE_SIZE, vk::DeviceSize offset = 0);
	void unmap() noexcept;

	void writeToBuffer(const void* data, const vk::DeviceSize size = VK_WHOLE_SIZE, const vk::DeviceSize offset = 0) noexcept;
	void flush(vk::DeviceSize size = VK_WHOLE_SIZE, vk::DeviceSize offset = 0);
	[[nodiscard]]
	vk::DescriptorBufferInfo descriptorInfo(vk::DeviceSize size = VK_WHOLE_SIZE, vk::DeviceSize offset = 0) noexcept;
	void invalidate(vk::DeviceSize size = VK_WHOLE_SIZE, vk::DeviceSize offset = 0);

	void writeToIndex(void* data, vk::DeviceSize index) noexcept;
	void flushIndex(vk::DeviceSize index);
	[[nodiscard]]
	vk::DescriptorBufferInfo descriptorInfoForIndex(vk::DeviceSize index) noexcept;
	void invalidateIndex(vk::DeviceSize index);

	/*getters*/
	[[nodiscard]]
	vk::Buffer getBuffer() const noexcept { return *_buffer; }
	[[nodiscard]]
	void* getMappedMemory() const noexcept { return _mapped; }
	[[nodiscard]]
	uint32_t getInstanceCount() const noexcept { return _instanceCount; }
	[[nodiscard]]
	vk::DeviceSize getInstanceSize() const noexcept { return _instanceSize; }
	[[nodiscard]]
	vk::DeviceSize getAlignmentSize() const noexcept { return _instanceSize; }
	[[nodiscard]]
	vk::BufferUsageFlags getUsageFlags() const noexcept { return _usageFlags; }
	[[nodiscard]]
	vk::MemoryPropertyFlags getMemoryPropertyFlags() const noexcept { return _memoryPropertyFlags; }
	[[nodiscard]]
	vk::DeviceSize getBufferSize() const noexcept { return _bufferSize; }

private:
	[[nodiscard]]
	static vk::DeviceSize getAlignment(vk::DeviceSize instanceSize, vk::DeviceSize minOffsetAlignment) noexcept;

private:
	void* _mapped = nullptr;
	vk::raii::DeviceMemory _memory = nullptr;
	vk::raii::Buffer _buffer = nullptr;

	vk::DeviceSize _bufferSize = 0;
	uint32_t _instanceCount = 0;
	vk::DeviceSize _instanceSize = 0;
	vk::DeviceSize _alignmentSize = 0;
	vk::BufferUsageFlags _usageFlags = vk::BufferUsageFlagBits(0);
	vk::MemoryPropertyFlags _memoryPropertyFlags = vk::MemoryPropertyFlagBits(0);
};

}