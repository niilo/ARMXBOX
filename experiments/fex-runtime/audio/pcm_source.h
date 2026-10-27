// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <span>
namespace armxbox {
constexpr uint32_t PcmRate = 48000;
constexpr uint32_t PcmSourceFrames = 24000; // Finite diagnostic, 0.5 seconds.
constexpr uint32_t PcmSeedBytes = 128; // 64 mono S16LE guest frames.
constexpr uint32_t PcmMaxCallbackFrames = 16384;
struct PcmRenderResult {
    uint32_t copiedFrames = 0, silentFrames = 0;
    bool cancelled = false, valid = false;
};
struct PcmSnapshot {
    uint64_t generation = 0, callbacks = 0, offeredFrames = 0;
    uint64_t copiedFrames = 0, silentFrames = 0;
    uint64_t invalidCallbacks = 0, staleCallbacks = 0, position = 0;
    uint32_t maxCallbackFrames = 0;
    bool cancelled = false, prepared = false;
};
// Controller prepares only while callbacks are quiescent. Preparation owns a
// complete finite sample copy; it does no work in the real-time callback.
// One serialized callback owns cursor. The controller may cancel/read metrics
// concurrently, but must retain this object through stream close and
// quiescence.
class PcmSource {
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    std::array<int16_t, PcmSourceFrames> samples{};
    uint32_t cursor = 0;
    // Token and cancel bit are a single CAS domain: an old cancellation can
    // never cancel a newly prepared generation, even if they race.
    std::atomic<uint64_t> state{};
    std::atomic<uint64_t> callbacks{}, offered{}, copied{}, silent{};
    std::atomic<uint64_t> invalid{}, stale{}, position{};
    std::atomic<uint32_t> maximum{};

public:
    static constexpr uint64_t MaxGeneration = UINT64_MAX >> 1;
    PcmSource() = default;
    PcmSource(const PcmSource &) = delete;
    PcmSource &operator=(const PcmSource &) = delete;
    // Rejects malformed seeds and non-increasing/unsupported generation values
    // transactionally. The 128-byte input is not retained. No fade/conversion
    // beyond S16LE decoding is implied: this repeats the exact guest waveform.
    bool prepare(std::span<const uint8_t>, uint64_t generation) noexcept;
    bool cancel(uint64_t generation) noexcept;
    // Valid calls initialize every requested frame. Invalid/stale calls write
    // nothing and return valid=false; the stream adapter must stop/error them.
    // No allocation, locks, logging, timing calls, modulo or per-sample work.
    PcmRenderResult render(int16_t *, int32_t frames,
                           uint64_t generation) noexcept;
    // Individual atomic counters are safe to observe, not one transactional
    // multi-field snapshot. Read after callback quiescence for final totals.
    PcmSnapshot snapshot() const noexcept;
};
} // namespace armxbox
