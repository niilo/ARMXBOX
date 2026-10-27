// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace armxbox {
constexpr uint32_t OutputCommit =
    0x54494d43; // Diagnostic CMIT, not an Xbox ordinal.
constexpr size_t OutputWireSize = 0x180;
using OutputFixture = std::array<uint8_t, OutputWireSize>;
OutputFixture outputFixture(uint32_t base, uint32_t sequence = 1);
constexpr size_t OutputProgramBytes = 706;
constexpr unsigned OutputInstructions = 72;
std::array<uint8_t, OutputProgramBytes> outputProgram(uint32_t recordBase);
struct StoppedOutputView {
    uint32_t base;
    std::span<const uint8_t> bytes;
    uint64_t generation;
    bool stopped, cancelled;
};
enum class OutputError {
    None,
    Full,
    NotStopped,
    Cancelled,
    Generation,
    Bounds,
    Incomplete,
    Format,
    Sequence,
    Layout,
    Vertex
};
class OutputPacket {
    friend class OutputQueue;
    std::array<uint8_t, 24> vertexData{};
    std::array<uint8_t, 64> textureData{};
    std::array<uint8_t, 128> pcmData{};
    uint64_t ownerGeneration = 0;
    uint32_t serial = 0;

public:
    std::span<const uint8_t> vertices() const
    {
        return vertexData;
    }
    std::span<const uint8_t> texture() const
    {
        return textureData;
    }
    std::span<const uint8_t> pcm() const
    {
        return pcmData;
    }
    uint64_t generation() const
    {
        return ownerGeneration;
    }
    uint32_t sequence() const
    {
        return serial;
    }
};
struct OutputLease {
    uint64_t generation = 0;
    uint32_t sequence = 0;
    unsigned slot = 0;
    const void *owner = nullptr;
};
// Externally serialized, two reusable slots. Borrowed packet access is valid
// until retire; this is not a concurrent audio callback queue.
class OutputQueue {
    std::array<OutputPacket, 2> slots;
    unsigned head = 0, count = 0;
    bool active = false;
    uint64_t generation;
    uint32_t lastSequence = 0;

public:
    explicit OutputQueue(uint64_t owner = 1) : generation(owner)
    {
    }
    OutputQueue(const OutputQueue &) = delete;
    OutputQueue &operator=(const OutputQueue &) = delete;
    OutputError submit(const StoppedOutputView &, uint32_t record);
    OutputLease acquire();
    const OutputPacket *get(OutputLease) const;
    bool retire(OutputLease);
    bool reset(uint64_t newGeneration);
    size_t size() const
    {
        return count;
    }
};
bool outputMatches(const OutputPacket &, uint32_t sequence);
} // namespace armxbox
