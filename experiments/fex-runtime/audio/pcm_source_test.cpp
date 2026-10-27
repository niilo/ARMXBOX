// SPDX-License-Identifier: GPL-2.0-or-later
#include "pcm_source.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <stdexcept>
#include <thread>
using namespace armxbox;
namespace {
unsigned controls = 0;
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
void pass(const char *message)
{
    ++controls;
    std::printf("PASS PCM source %s\n", message);
}
using Seed = std::array<uint8_t, PcmSeedBytes>;
Seed makeSeed(int variant = 0)
{
    Seed bytes{};
    for (unsigned i = 0; i < 64; ++i) {
        const uint16_t sample = variant ?
                                    uint16_t((i * 1103 + 0x8000) & 0xffff) :
                                    uint16_t((int(i % 16) - 8) * 512);
        bytes[2 * i] = sample;
        bytes[2 * i + 1] = sample >> 8;
    }
    return bytes;
}
int16_t expected(const Seed &seed, uint64_t frame)
{
    const unsigned i = frame % 64;
    return std::bit_cast<int16_t>(
        uint16_t(uint16_t(seed[2 * i]) | uint16_t(seed[2 * i + 1]) << 8));
}
void checkChunk(const int16_t *samples, uint32_t frames, uint64_t before,
                PcmRenderResult rendered, const Seed &seed)
{
    require(rendered.valid, "valid callback rejected");
    require(rendered.copiedFrames + rendered.silentFrames == frames,
            "callback frame totals do not cover output");
    for (uint32_t i = 0; i < frames; ++i)
        require(
            samples[i] ==
                (i < rendered.copiedFrames ? expected(seed, before + i) : 0),
            "callback output differs from independent LE/repetition/tail "
            "oracle");
}
} // namespace
int main()
{
    try {
        PcmSource source;
        auto seed = makeSeed();
        auto snapshot = source.snapshot();
        require(!snapshot.prepared && snapshot.generation == 0 &&
                    !snapshot.cancelled,
                "new source is unexpectedly prepared");
        require(!source.prepare({}, 1) &&
                    !source.prepare(std::span(seed).first(127), 1),
                "short seed admitted");
        std::array<uint8_t, 129> tooLong{};
        require(!source.prepare(tooLong, 1) && !source.prepare(seed, 0) &&
                    !source.prepare(seed, PcmSource::MaxGeneration + 1),
                "long seed or invalid generation admitted");
        require(!source.snapshot().prepared, "invalid prepare published state");
        pass("malformed seed and invalid generation have no publication");

        require(source.prepare(seed, 1), "first seed rejected");
        snapshot = source.snapshot();
        require(snapshot.prepared && snapshot.generation == 1 &&
                    !snapshot.callbacks && !snapshot.position &&
                    !snapshot.cancelled,
                "prepared metadata incorrect");
        pass("finite source publishes one owned generation");
        require(!source.prepare(makeSeed(1), 1),
                "repeated generation admitted");
        require(!source.prepare({}, 2), "invalid replacement admitted");
        require(source.snapshot().generation == 1,
                "failed replacement changed generation");
        pass("failed or repeated preparation is transactional");

        auto original = seed;
        seed.fill(0x39);
        std::array<int16_t, PcmMaxCallbackFrames + 2> output{};
        uint64_t offered = 0, copied = 0, callbacks = 0;
        const std::array<int32_t, 8> chunks{ 1,    3,    191,   257,
                                             1024, 8191, 16384, 64 };
        for (auto frames : chunks) {
            output.fill(0x1234);
            const auto result = source.render(output.data() + 1, frames, 1);
            checkChunk(output.data() + 1, frames, copied, result, original);
            require(!result.cancelled,
                    "ordinary completion mistaken for cancellation");
            require(output.front() == 0x1234 && output[frames + 1] == 0x1234,
                    "callback wrote beyond supplied frame range");
            copied += result.copiedFrames;
            offered += frames;
            ++callbacks;
        }
        snapshot = source.snapshot();
        require(copied == PcmSourceFrames &&
                    snapshot.position == PcmSourceFrames &&
                    snapshot.copiedFrames == PcmSourceFrames &&
                    snapshot.offeredFrames == offered &&
                    snapshot.silentFrames == offered - PcmSourceFrames &&
                    snapshot.callbacks == callbacks &&
                    snapshot.maxCallbackFrames == PcmMaxCallbackFrames &&
                    !snapshot.invalidCallbacks && !snapshot.staleCallbacks,
                "final callback counters disagree with actual chunks");
        pass("variable chunks reproduce exact LE seed after source overwrite");
        pass("partial final callback and repeated post-end calls fully zero "
             "tail");
        pass("callback range canaries and final frame counters agree");

        const auto beforeInvalid = source.snapshot();
        output.fill(0x1357);
        require(!source.render(output.data(), 0, 1).valid &&
                    !source.render(output.data(), -1, 1).valid &&
                    !source.render(output.data(), PcmMaxCallbackFrames + 1, 1)
                         .valid &&
                    !source.render(nullptr, 1, 1).valid,
                "invalid callback admitted");
        require(std::all_of(output.begin(), output.end(),
                            [](auto value) { return value == 0x1357; }),
                "invalid callback wrote output");
        snapshot = source.snapshot();
        require(snapshot.invalidCallbacks == 4 &&
                    snapshot.position == beforeInvalid.position &&
                    snapshot.callbacks == beforeInvalid.callbacks &&
                    snapshot.offeredFrames == beforeInvalid.offeredFrames,
                "invalid callback advanced finite source or regular counters");
        pass(
            "null zero negative and oversized callbacks reject without writes");

        require(source.prepare(original, 2), "second generation rejected");
        require(source.cancel(2) && source.cancel(2),
                "same generation cancellation not idempotent");
        output.fill(99);
        auto result = source.render(output.data(), 67, 2);
        checkChunk(output.data(), 67, 0, result, original);
        require(result.cancelled && result.copiedFrames == 0 &&
                    result.silentFrames == 67,
                "pre-cancelled source copied content");
        require(source.snapshot().copiedFrames == 0 &&
                    source.snapshot().position == 0,
                "cancelled callback advanced content");
        pass("pre-entry cancellation emits silence without source progress");

        require(source.prepare(original, 3), "third generation rejected");
        result = source.render(output.data(), 13, 3);
        checkChunk(output.data(), 13, 0, result, original);
        require(source.cancel(3), "mid-source cancellation failed");
        result = source.render(output.data(), 191, 3);
        checkChunk(output.data(), 191, 13, result, original);
        snapshot = source.snapshot();
        require(snapshot.copiedFrames == 13 && snapshot.position == 13 &&
                    snapshot.silentFrames == 191 &&
                    snapshot.offeredFrames == 204 && snapshot.cancelled,
                "mid-source stop lost exact source/tail counters");
        pass("mid-source cancellation retains committed cursor and zeros "
             "remainder");

        const auto second = makeSeed(1);
        require(source.prepare(second, 4), "new seed preparation rejected");
        snapshot = source.snapshot();
        require(!snapshot.cancelled && !snapshot.callbacks &&
                    !snapshot.invalidCallbacks && !snapshot.staleCallbacks &&
                    snapshot.position == 0,
                "new generation retained old cancellation/counters");
        output.fill(0x4567);
        require(!source.render(output.data(), 64, 3).valid &&
                    !source.render(output.data(), 64, 0).valid,
                "stale callback token admitted");
        require(!source.cancel(3) && !source.cancel(0) &&
                    !source.snapshot().cancelled,
                "stale cancellation affected new source");
        require(source.snapshot().staleCallbacks == 2 &&
                    !source.snapshot().position &&
                    std::all_of(output.begin(), output.end(),
                                [](auto value) { return value == 0x4567; }),
                "stale callback mutated data or source cursor");
        result = source.render(output.data(), 129, 4);
        checkChunk(output.data(), 129, 0, result, second);
        pass("new generation resets state and rejects stale callbacks and "
             "cancellation");
        pass(
            "replacement seed preserves full S16 signed range and LE decoding");

        // A stale controller cancellation may race new preparation even when
        // data callbacks are quiescent. Atomic token+cancel state prevents an
        // old request from leaking into the new generation.
        for (uint64_t generation = 5; generation < 261; ++generation) {
            std::atomic<bool> go{};
            std::thread lateCancel([&] {
                while (!go.load(std::memory_order_acquire)) {
                }
                for (unsigned i = 0; i < 64; ++i)
                    source.cancel(generation - 1);
            });
            go.store(true, std::memory_order_release);
            require(source.prepare(original, generation),
                    "race preparation failed");
            lateCancel.join();
            require(!source.snapshot().cancelled &&
                        source.snapshot().generation == generation,
                    "stale cancellation contaminated new generation");
        }
        pass("stale cancellation racing quiescent preparation cannot cancel "
             "replacement");

        require(source.prepare(original, PcmSource::MaxGeneration),
                "maximum generation rejected");
        require(!source.prepare(original, 1) &&
                    source.cancel(PcmSource::MaxGeneration),
                "generation rollover or maximum cancellation incorrect");
        require(source.snapshot().generation == PcmSource::MaxGeneration &&
                    source.snapshot().cancelled,
                "packed generation/cancellation lost high bits");
        pass("maximum generation remains cancellable and refuses rollover");
        std::printf("PASS PCM source: %u finite callback ownership controls\n",
                    controls);
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL PCM source: %s\n", error.what());
        return 1;
    }
}
