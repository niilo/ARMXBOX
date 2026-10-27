// SPDX-License-Identifier: GPL-2.0-or-later
#include "pcm_source.h"
#include <algorithm>
#include <bit>
#include <cstring>
namespace armxbox {
bool PcmSource::prepare(std::span<const uint8_t> bytes,
                        uint64_t generation) noexcept
{
    if (bytes.size() != PcmSeedBytes || !generation ||
        generation > MaxGeneration ||
        generation <= (state.load(std::memory_order_acquire) >> 1))
        return false;
    std::array<int16_t, PcmSeedBytes / 2> seed;
    for (unsigned i = 0; i < seed.size(); ++i) {
        const uint16_t bits =
            uint16_t(bytes[2 * i]) | (uint16_t(bytes[2 * i + 1]) << 8);
        seed[i] = std::bit_cast<int16_t>(bits);
    }
    // Host/controller-only expansion. Callback work is one contiguous copy.
    for (size_t at = 0; at < samples.size(); at += seed.size())
        std::memcpy(samples.data() + at, seed.data(),
                    std::min(seed.size(), samples.size() - at) *
                        sizeof(int16_t));
    cursor = 0;
    callbacks.store(0, std::memory_order_relaxed);
    offered.store(0, std::memory_order_relaxed);
    copied.store(0, std::memory_order_relaxed);
    silent.store(0, std::memory_order_relaxed);
    invalid.store(0, std::memory_order_relaxed);
    stale.store(0, std::memory_order_relaxed);
    position.store(0, std::memory_order_relaxed);
    maximum.store(0, std::memory_order_relaxed);
    state.store(generation << 1, std::memory_order_release);
    return true;
}
bool PcmSource::cancel(uint64_t generation) noexcept
{
    if (!generation || generation > MaxGeneration)
        return false;
    uint64_t expected = generation << 1;
    if (state.compare_exchange_strong(expected, expected | 1,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
        return true;
    return expected == ((generation << 1) | 1); // Idempotent same-session stop.
}
PcmRenderResult PcmSource::render(int16_t *output, int32_t frames,
                                  uint64_t generation) noexcept
{
    const uint64_t observed = state.load(std::memory_order_acquire);
    if (!generation || (observed >> 1) != generation) {
        stale.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    if (!output || frames <= 0 || uint32_t(frames) > PcmMaxCallbackFrames) {
        invalid.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    const uint32_t count = uint32_t(frames);
    const bool cancelled = observed & 1;
    const uint32_t source =
        cancelled ? 0 : std::min(count, PcmSourceFrames - cursor);
    if (source)
        std::memcpy(output, samples.data() + cursor,
                    size_t(source) * sizeof(int16_t));
    if (count > source)
        std::memset(output + source, 0,
                    size_t(count - source) * sizeof(int16_t));
    cursor += source;
    callbacks.fetch_add(1, std::memory_order_relaxed);
    offered.fetch_add(count, std::memory_order_relaxed);
    copied.fetch_add(source, std::memory_order_relaxed);
    silent.fetch_add(count - source, std::memory_order_relaxed);
    position.store(cursor, std::memory_order_release);
    if (count > maximum.load(std::memory_order_relaxed))
        maximum.store(count, std::memory_order_relaxed);
    return { source, count - source, cancelled, true };
}
PcmSnapshot PcmSource::snapshot() const noexcept
{
    PcmSnapshot result;
    const auto observed = state.load(std::memory_order_acquire);
    result.generation = observed >> 1;
    result.prepared = result.generation != 0;
    result.cancelled = observed & 1;
    result.callbacks = callbacks.load(std::memory_order_relaxed);
    result.offeredFrames = offered.load(std::memory_order_relaxed);
    result.copiedFrames = copied.load(std::memory_order_relaxed);
    result.silentFrames = silent.load(std::memory_order_relaxed);
    result.invalidCallbacks = invalid.load(std::memory_order_relaxed);
    result.staleCallbacks = stale.load(std::memory_order_relaxed);
    result.position = position.load(std::memory_order_acquire);
    result.maxCallbackFrames = maximum.load(std::memory_order_relaxed);
    return result;
}
} // namespace armxbox
