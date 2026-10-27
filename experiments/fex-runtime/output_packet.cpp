// SPDX-License-Identifier: GPL-2.0-or-later
#include "output_packet.h"
#include <algorithm>
#include <cstring>
namespace armxbox {
namespace {
uint32_t read(std::span<const uint8_t> bytes, size_t at)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= uint32_t(bytes[at + i]) << (8 * i);
    return value;
}
void put(OutputFixture &bytes, size_t at, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = value >> (8 * i);
}
bool range(const StoppedOutputView &view, uint32_t address, size_t size)
{
    if (view.bytes.size() > (1ULL << 32) - view.base || address < view.base)
        return false;
    const uint64_t offset = uint64_t(address) - view.base;
    return offset <= view.bytes.size() && size <= view.bytes.size() - offset;
}
} // namespace
OutputFixture outputFixture(uint32_t base, uint32_t sequence)
{
    OutputFixture bytes{};
    const uint32_t fields[] = {
        0x3154554f, 1, sequence, base + 0x80,  24, base + 0xa0,
        64,         4, 4,        base + 0x100, 64, OutputCommit,
        1,          1, 1,        48000
    };
    for (unsigned i = 0; i < 16; ++i)
        put(bytes, 4 * i, fields[i]);
    const uint32_t vertices[] = { 0xbf800000, 0xbf800000, 0xbf800000,
                                  0x3f800000, 0x3f800000, 0xbf800000 };
    for (unsigned i = 0; i < 6; ++i)
        put(bytes, 0x80 + 4 * i, vertices[i]);
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x) {
            const size_t at = 0xa0 + 4 * (y * 4 + x);
            const bool odd = (x ^ y) & 1;
            bytes[at] = odd ? 240 : 16;
            bytes[at + 1] = odd ? 32 : 208;
            bytes[at + 2] = odd ? 80 : 96;
            bytes[at + 3] = 255;
        }
    for (unsigned i = 0; i < 64; ++i) {
        const uint16_t sample = uint16_t((int(i % 16) - 8) * 512);
        bytes[0x100 + 2 * i] = sample;
        bytes[0x101 + 2 * i] = sample >> 8;
    }
    return bytes;
}
std::array<uint8_t, OutputProgramBytes> outputProgram(uint32_t base)
{
    std::array<uint8_t, OutputProgramBytes> code{};
    const auto data = outputFixture(base);
    size_t cursor = 0;
    auto imm = [&](uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            code[cursor++] = value >> (8 * i);
    };
    auto store = [&](unsigned offset, uint32_t value) {
        code[cursor++] = 0xc7;
        code[cursor++] = 0x05;
        imm(base + offset);
        imm(value);
    };
    store(44, 0); // Withdraw old publication before producing the next packet.
    code[cursor++] = 0xa3;
    imm(base + 8); // Sequence arrives in guest EAX.
    for (unsigned at = 0; at < 64; at += 4)
        if (at != 8 && at != 44)
            store(at, read(data, at));
    for (auto [begin, end] : std::array<std::array<unsigned, 2>, 3>{
             { { 0x80, 0x98 }, { 0xa0, 0xe0 }, { 0x100, 0x180 } } })
        for (unsigned at = begin; at < end; at += 4)
            store(at, read(data, at));
    store(44, OutputCommit); // Commit only after every payload store.
    code[cursor++] = 0xf4;
    return code;
}
OutputError OutputQueue::submit(const StoppedOutputView &view, uint32_t record)
{
    // Reject before reading guest memory when publication is ineligible/full.
    if (count == slots.size())
        return OutputError::Full;
    if (!view.stopped)
        return OutputError::NotStopped;
    if (view.cancelled)
        return OutputError::Cancelled;
    if (!generation || view.generation != generation)
        return OutputError::Generation;
    if ((record & 3) || !range(view, record, 64))
        return OutputError::Bounds;
    const auto header = view.bytes.subspan(uint64_t(record) - view.base, 64);
    if (read(header, 44) != OutputCommit)
        return OutputError::Incomplete;
    if (read(header, 0) != 0x3154554f || read(header, 4) != 1 ||
        read(header, 16) != 24 || read(header, 24) != 64 ||
        read(header, 28) != 4 || read(header, 32) != 4 ||
        read(header, 40) != 64 || read(header, 48) != 1 ||
        read(header, 52) != 1 || read(header, 56) != 1 ||
        read(header, 60) != 48000)
        return OutputError::Format;
    const uint32_t serial = read(header, 8);
    if (!serial || serial <= lastSequence)
        return OutputError::Sequence;
    const std::array<uint32_t, 4> addresses{ record, read(header, 12),
                                             read(header, 20),
                                             read(header, 36) };
    const std::array<size_t, 4> lengths{ 64, 24, 64, 128 };
    for (unsigned i = 0; i < 4; ++i) {
        if (!range(view, addresses[i], lengths[i]))
            return OutputError::Bounds;
        if (addresses[i] & (i == 3 ? 1 : 3))
            return OutputError::Layout;
        for (unsigned j = 0; j < i; ++j)
            if (uint64_t(addresses[i]) < uint64_t(addresses[j]) + lengths[j] &&
                uint64_t(addresses[j]) < uint64_t(addresses[i]) + lengths[i])
                return OutputError::Layout;
    }
    const auto vertices =
        view.bytes.subspan(uint64_t(addresses[1]) - view.base, 24);
    for (unsigned i = 0; i < 6; ++i) {
        // IEEE binary32 magnitude ordering rejects NaN/Inf and |x|>1
        // without relying on host floating-point modes or fast-math flags.
        if ((read(vertices, 4 * i) & 0x7fffffffU) > 0x3f800000U)
            return OutputError::Vertex;
    }
    // Every validation precedes writes. Copy directly into an unpublished slot
    // once, then publish its sequence/count. No guest pointer is retained.
    auto &packet = slots[(head + count) % slots.size()];
    std::memcpy(packet.vertexData.data(), vertices.data(), 24);
    std::memcpy(packet.textureData.data(),
                view.bytes.data() + (uint64_t(addresses[2]) - view.base), 64);
    std::memcpy(packet.pcmData.data(),
                view.bytes.data() + (uint64_t(addresses[3]) - view.base), 128);
    packet.ownerGeneration = generation;
    packet.serial = serial;
    lastSequence = serial;
    ++count;
    return OutputError::None;
}
OutputLease OutputQueue::acquire()
{
    if (active || !count)
        return {};
    active = true;
    return { generation, slots[head].serial, head, this };
}
const OutputPacket *OutputQueue::get(OutputLease lease) const
{
    return lease.owner == this && active && count && lease.slot == head &&
                   lease.generation == generation &&
                   lease.sequence == slots[head].serial ?
               &slots[head] :
               nullptr;
}
bool OutputQueue::retire(OutputLease lease)
{
    if (!get(lease))
        return false;
    active = false;
    head = (head + 1) % slots.size();
    --count;
    return true;
}
bool OutputQueue::reset(uint64_t next)
{
    if (active || count || next <= generation)
        return false;
    generation = next;
    lastSequence = 0;
    head = 0;
    return true;
}
bool outputMatches(const OutputPacket &p, uint32_t sequence)
{
    const auto expected = outputFixture(0, sequence);
    return p.sequence() == sequence &&
           std::equal(p.vertices().begin(), p.vertices().end(),
                      expected.begin() + 0x80) &&
           std::equal(p.texture().begin(), p.texture().end(),
                      expected.begin() + 0xa0) &&
           std::equal(p.pcm().begin(), p.pcm().end(), expected.begin() + 0x100);
}
} // namespace armxbox
