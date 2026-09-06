#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <functional>

namespace renderer {

class Hash {
public:
	static constexpr std::size_t Size = 32;
	using Storage = std::array<uint8_t, Size>;

	class Builder;

	struct Hasher {
		std::size_t operator()(const Hash& hash) const noexcept;
	};

	Hash() = default;
	Hash(std::span<const uint8_t> input) noexcept;
	Hash(std::string_view input) noexcept;

	[[nodiscard]]
	const Storage& bytes() const noexcept { return _bytes; }

	bool operator==(const Hash&) const noexcept = default;

private:
	explicit Hash(Storage bytes) noexcept
		: _bytes(std::move(bytes)) {}

	Storage _bytes{};
};

// The hash is not portable across big-endian and little-endian systems
class Hash::Builder {
public:
	Builder() noexcept;
	~Builder() = default;

	Builder(const Builder&) = delete;
	Builder& operator=(const Builder&) = delete;
	Builder(Builder&&) noexcept = default;
	Builder& operator=(Builder&&) noexcept = default;

	void update(std::span<const uint8_t> input) noexcept;
	void update(std::string_view input) noexcept;

	template <std::integral Integer>
	void update(Integer value) noexcept {
		update(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&value), sizeof(value)));
	}

	template <std::floating_point FloatingPoint>
	void update(FloatingPoint value) noexcept {
		if (value == FloatingPoint{ 0 }) {
			value = FloatingPoint{ 0 };
		}
		update(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&value), sizeof(value)));
	}

	[[nodiscard]]
	Hash finish() const noexcept;

private:
	using ChainingValue = std::array<uint32_t, 8>;
	static constexpr size_t BlockSize = 64;
	static constexpr size_t ChunkSize = 1024;
	static constexpr size_t MaximumTreeDepth = 54;

	struct ChunkState {
		[[nodiscard]]
		size_t length() const noexcept {
			return BlockSize * blocksCompressed + blockLength;
		}

		ChainingValue chainingValue{};
		uint64_t counter = 0;
		std::array<uint8_t, BlockSize> block{};
		size_t blockLength = 0;
		uint8_t blocksCompressed = 0;
	};

	void compressBlock() noexcept;
	void completeChunk() noexcept;
	void resetChunk() noexcept;
	void updateChunk(std::span<const uint8_t> input) noexcept;
	void addChunkChainingValue(ChainingValue newValue, uint64_t totalChunks) noexcept;

	ChunkState _chunk;
	std::array<ChainingValue, MaximumTreeDepth> _chainingValueStack{};
	size_t _stackSize = 0;
};

}

namespace std {

template <>
struct hash<renderer::Hash> {
	std::size_t operator()(const renderer::Hash& hash) const noexcept {
		renderer::Hash::Hasher hasher;
		return hasher(hash);
	}
};

}