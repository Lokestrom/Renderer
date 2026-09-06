#include "Hash.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <exception>
#include <utility>
#include <assert.h>
#include <iostream>

namespace renderer {

namespace {

constexpr size_t BlockSize = 64;

enum class BlakeFlag : uint32_t {
	None = 0,
	ChunkStart = 1 << 0,
	ChunkEnd = 1 << 1,
	Parent = 1 << 2,
	Root = 1 << 3,
};

constexpr BlakeFlag operator|(BlakeFlag left, BlakeFlag right) noexcept {
	return static_cast<BlakeFlag>(
		static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

BlakeFlag chunkStartFlag(uint8_t blocksCompressed) noexcept {
	return blocksCompressed == 0 ? BlakeFlag::ChunkStart : BlakeFlag::None;
}

using ChainingValue = std::array<uint32_t, 8>;
using BlockWords = std::array<uint32_t, 16>;
using CompressionOutput = std::array<uint32_t, 16>;

constexpr ChainingValue InitializationVector{
	0x6a09e667,
	0xbb67ae85,
	0x3c6ef372,
	0xa54ff53a,
	0x510e527f,
	0x9b05688c,
	0x1f83d9ab,
	0x5be0cd19
};

constexpr std::array<uint8_t, 16> MessagePermutation{
	2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8
};

uint32_t loadUint32(const uint8_t* bytes) noexcept {
	return static_cast<uint32_t>(bytes[0]) |
		   static_cast<uint32_t>(bytes[1]) << 8 |
		   static_cast<uint32_t>(bytes[2]) << 16 |
		   static_cast<uint32_t>(bytes[3]) << 24;
}

void storeUint32(uint8_t* bytes, uint32_t value) noexcept {
	bytes[0] = static_cast<uint8_t>(value);
	bytes[1] = static_cast<uint8_t>(value >> 8);
	bytes[2] = static_cast<uint8_t>(value >> 16);
	bytes[3] = static_cast<uint8_t>(value >> 24);
}

BlockWords loadBlockWords(const std::array<uint8_t, BlockSize>& block) noexcept {
	BlockWords words{};
	for (size_t i = 0; i < words.size(); ++i) {
		words[i] = loadUint32(block.data() + i * 4);
	}
	return words;
}

void mix(
	std::array<uint32_t, 16>& state,
	size_t a,
	size_t b,
	size_t c,
	size_t d,
	uint32_t firstMessage,
	uint32_t secondMessage) noexcept {
	state[a] = state[a] + state[b] + firstMessage;
	state[d] = std::rotr(state[d] ^ state[a], 16);
	state[c] += state[d];
	state[b] = std::rotr(state[b] ^ state[c], 12);
	state[a] = state[a] + state[b] + secondMessage;
	state[d] = std::rotr(state[d] ^ state[a], 8);
	state[c] += state[d];
	state[b] = std::rotr(state[b] ^ state[c], 7);
}

void round(std::array<uint32_t, 16>& state, const BlockWords& message) noexcept {
	mix(state, 0, 4, 8, 12, message[0], message[1]);
	mix(state, 1, 5, 9, 13, message[2], message[3]);
	mix(state, 2, 6, 10, 14, message[4], message[5]);
	mix(state, 3, 7, 11, 15, message[6], message[7]);
	mix(state, 0, 5, 10, 15, message[8], message[9]);
	mix(state, 1, 6, 11, 12, message[10], message[11]);
	mix(state, 2, 7, 8, 13, message[12], message[13]);
	mix(state, 3, 4, 9, 14, message[14], message[15]);
}

void permute(BlockWords& message) noexcept {
	const BlockWords original = message;
	for (size_t i = 0; i < message.size(); ++i) {
		message[i] = original[MessagePermutation[i]];
	}
}

CompressionOutput compress(
	const ChainingValue& chainingValue,
	BlockWords message,
	uint64_t counter,
	uint32_t blockLength,
	BlakeFlag flags) noexcept {
	std::array<uint32_t, 16> state{
		chainingValue[0], chainingValue[1], chainingValue[2], chainingValue[3],
		chainingValue[4], chainingValue[5], chainingValue[6], chainingValue[7],
		InitializationVector[0], InitializationVector[1], InitializationVector[2], InitializationVector[3],
		static_cast<uint32_t>(counter), static_cast<uint32_t>(counter >> 32), blockLength,
		static_cast<uint32_t>(flags)
	};

	for (size_t roundIndex = 0; roundIndex < 7; ++roundIndex) {
		round(state, message);
		if (roundIndex != 6) {
			permute(message);
		}
	}

	for (size_t i = 0; i < 8; ++i) {
		state[i] ^= state[i + 8];
		state[i + 8] ^= chainingValue[i];
	}
	return state;
}

struct Output {
	ChainingValue inputChainingValue;
	BlockWords blockWords;
	uint64_t counter;
	uint32_t blockLength;
	BlakeFlag flags;

	ChainingValue chainingValue() const noexcept {
		const CompressionOutput words = compress(inputChainingValue, blockWords, counter, blockLength, flags);
		ChainingValue result{};
		std::copy_n(words.begin(), result.size(), result.begin());
		return result;
	}

	Hash::Storage rootBytes() const noexcept {
		const CompressionOutput words = compress(
			inputChainingValue,
			blockWords,
			0,
			blockLength,
			flags | BlakeFlag::Root);
		std::array<uint8_t, 64> outputBlock{};
		for (size_t i = 0; i < words.size(); ++i) {
			storeUint32(outputBlock.data() + i * 4, words[i]);
		}

		Hash::Storage result{};
		std::copy_n(outputBlock.begin(), result.size(), result.begin());
		return result;
	}
};

Output parentOutput(
	const ChainingValue& leftChild,
	const ChainingValue& rightChild) noexcept {
	BlockWords blockWords{};
	std::copy(leftChild.begin(), leftChild.end(), blockWords.begin());
	std::copy(rightChild.begin(), rightChild.end(), blockWords.begin() + leftChild.size());
	return {
		.inputChainingValue = InitializationVector,
		.blockWords = blockWords,
		.counter = 0,
		.blockLength = BlockSize,
		.flags = BlakeFlag::Parent
	};
}

}

std::size_t Hash::Hasher::operator()(const Hash& hash) const noexcept {
	std::size_t result = 0;
	for (const uint8_t byte : hash.bytes()) {
		result = result * 7919 + byte;
	}
	return result;
}

Hash::Hash(std::span<const uint8_t> input) noexcept {
	Builder builder;
	builder.update(input);
	*this = builder.finish();
}

Hash::Hash(std::string_view input) noexcept {
	Builder builder;
	builder.update(input);
	*this = builder.finish();
}

Hash::Builder::Builder() noexcept {
	resetChunk();
	_chunk.counter = 0;
}

void Hash::Builder::addChunkChainingValue(ChainingValue newValue, uint64_t totalChunks) noexcept {
	while ((totalChunks & 1) == 0) {
		newValue = parentOutput(
			_chainingValueStack[--_stackSize], newValue)
					   .chainingValue();
		totalChunks >>= 1;
	}
	assert(_stackSize < MaximumTreeDepth && "Blake3: stack overflow, the implementation can only handle 2^64 - 1 bytes");
	_chainingValueStack[_stackSize++] = newValue;
}

void Hash::Builder::compressBlock() noexcept {
	const Output output{
		.inputChainingValue = _chunk.chainingValue,
		.blockWords = loadBlockWords(_chunk.block),
		.counter = _chunk.counter,
		.blockLength = BlockSize,
		.flags = chunkStartFlag(_chunk.blocksCompressed)
	};
	_chunk.chainingValue = output.chainingValue();
	_chunk.blocksCompressed++;
	_chunk.block.fill(0);
	_chunk.blockLength = 0;
}

void Hash::Builder::resetChunk() noexcept {
	_chunk.chainingValue = InitializationVector;
	_chunk.block.fill(0);
	_chunk.blockLength = 0;
	_chunk.blocksCompressed = 0;
}

void Hash::Builder::completeChunk() noexcept {
	const Output output{
		.inputChainingValue = _chunk.chainingValue,
		.blockWords = loadBlockWords(_chunk.block),
		.counter = _chunk.counter,
		.blockLength = static_cast<uint32_t>(_chunk.blockLength),
		.flags = chunkStartFlag(_chunk.blocksCompressed) | BlakeFlag::ChunkEnd
	};
	_chunk.counter++;
	addChunkChainingValue(output.chainingValue(), _chunk.counter);
	resetChunk();
}

void Hash::Builder::updateChunk(std::span<const uint8_t> input) noexcept {
	while (!input.empty()) {
		if (_chunk.blockLength == BlockSize) {
			compressBlock();
		}

		const size_t take = std::min(input.size(), BlockSize - _chunk.blockLength);
		std::memcpy(_chunk.block.data() + _chunk.blockLength, input.data(), take);
		_chunk.blockLength += take;
		input = input.subspan(take);
	}
}

void Hash::Builder::update(std::span<const uint8_t> input) noexcept {
	while (!input.empty()) {
		if (_chunk.length() == ChunkSize) {
			completeChunk();
		}

		const size_t take = std::min(input.size(), ChunkSize - _chunk.length());
		updateChunk(input.first(take));
		input = input.subspan(take);
	}
}

void Hash::Builder::update(std::string_view input) noexcept {
	update(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(input.data()), input.size()));
}

Hash Hash::Builder::finish() const noexcept {
	Output output{
		.inputChainingValue = _chunk.chainingValue,
		.blockWords = loadBlockWords(_chunk.block),
		.counter = _chunk.counter,
		.blockLength = static_cast<uint32_t>(_chunk.blockLength),
		.flags = chunkStartFlag(_chunk.blocksCompressed) | BlakeFlag::ChunkEnd
	};
	for (size_t i = _stackSize; i > 0; --i) {
		output = parentOutput(
			_chainingValueStack[i - 1],
			output.chainingValue());
	}

	Hash::Storage bytes = output.rootBytes();
	return Hash(bytes);
}

}
