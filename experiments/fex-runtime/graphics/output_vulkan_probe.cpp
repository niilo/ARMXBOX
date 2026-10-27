// SPDX-License-Identifier: GPL-2.0-or-later
#include "output_vulkan_probe.h"
#include "output_packet.h"
#include "output_vulkan.h"
#include "output_spirv.h"
#ifdef __ANDROID__
#include "audio/pcm_sink.h"
#endif
#include <bit>
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
namespace armxbox {
namespace {
constexpr const char *Labels[] = {
    "Vulkan receives both owned guest packets after image cleanup",
    "Vulkan initializes device and checked embedded shaders",
    "Vulkan cold guest packet matches scalar RGBA pixel oracle",
    "Vulkan warm guest packet matches scalar RGBA pixel oracle",
    "Vulkan reused slot produces byte-identical canonical frames",
    "Vulkan host asymmetric inset control matches scalar pixel oracle",
    "Vulkan completes fenced readback and explicit resource cleanup",
};
static_assert(std::size(Labels) == OutputGpuCheckCount);
struct Session {
    OutputGpuResults &out;
    void (*log)(const char *);
    Session(OutputGpuResults &result, void (*logger)(const char *))
        : out(result), log(logger)
    {
    }
    std::unique_ptr<OutputVulkan> gpu;
    OutputPixels first{};
    unsigned entries = 0;
    bool failed = false, cleaned = false;
    const char *stage = "before first guest packet";
    void note(const char *next) noexcept
    {
        stage = next;
        if (log)
            log(next); // Persist the last safe boundary before driver work.
    }
    void error(const char *message) noexcept
    {
        failed = true;
        char previous[sizeof out.detail];
        std::snprintf(previous, sizeof previous, "%s", out.detail);
        std::snprintf(out.detail, sizeof out.detail,
                      "%.511s; stage=%.127s; error=%.350s", previous, stage,
                      message);
    }
    void packet(const OutputPacket &packet, unsigned index)
    {
        if (failed)
            return;
        if (index != entries || index >= 2 || !outputMatches(packet, index + 1))
            throw std::runtime_error("unexpected owned guest packet identity");
        ++entries;
        if (!gpu) {
            note("Vulkan: initialize system loader, device and embedded "
                 "shaders");
            gpu = std::make_unique<OutputVulkan>(OutputVertexSpirv,
                                                 OutputFragmentSpirv, false);
            out.passed[1] = true;
            std::snprintf(out.detail, sizeof out.detail, "%s", gpu->detail());
        }
        note(index ?
                 "Vulkan: submit second owned guest packet and wait for "
                 "readback" :
                 "Vulkan: submit first owned guest packet after image cleanup");
        const auto pixels = gpu->render(packet);
        out.passed[2 + index] = outputPixelMismatches(pixels, packet) == 0;
        if (!index) {
#ifdef __ANDROID__
            // Copy accepted PCM into the independent playback session before
            // this immutable packet lease retires. Playback stays
            // user-triggered.
            if (out.passed[2]) {
                const bool captured = publishPcm(packet.pcm());
                if (log)
                    log(captured ? "PCM: accepted guest snapshot copied for "
                                   "explicit playback" :
                                   "PCM: guest snapshot capture unavailable");
            }
#endif
            first = pixels;
            return;
        }
        out.passed[4] = first == pixels;
        note("Vulkan: host asymmetric texture and inset triangle control");
        // This is explicitly host-constructed; the two canonical submissions
        // above came from native guest execution. It catches bugs hidden by
        // a symmetric checker or a driver ignoring supplied vertex data.
        auto source = outputFixture(0x13000, 3);
        for (unsigned i = 0; i < 16; ++i) {
            source[0xa0 + 4 * i] = 11 + 9 * i;
            source[0xa1 + 4 * i] = 197 - 7 * i;
            source[0xa2 + 4 * i] = 31 + 5 * i;
            source[0xa3 + 4 * i] = 83 + 11 * i;
        }
        const std::array<float, 6> inset{ -.75f, -.5f, -.75f, .75f, .5f, -.5f };
        for (unsigned i = 0; i < inset.size(); ++i) {
            const auto bits = std::bit_cast<uint32_t>(inset[i]);
            for (unsigned b = 0; b < 4; ++b)
                source[0x80 + 4 * i + b] = bits >> (8 * b);
        }
        OutputQueue queue;
        if (queue.submit({ 0x13000, source, 1, true, false }, 0x13000) !=
            OutputError::None)
            throw std::runtime_error("host control packet capture");
        const auto lease = queue.acquire();
        const auto *control = queue.get(lease);
        if (!control)
            throw std::runtime_error("host control packet lease");
        source.fill(0x39);
        const auto asymmetric = gpu->render(*control);
        out.passed[5] = outputPixelMismatches(asymmetric, *control) == 0;
        if (!queue.retire(lease))
            throw std::runtime_error("host control packet retirement");
        note("Vulkan: explicit final resource cleanup before second guest "
             "lease retirement");
        gpu->finish();
        cleaned = true;
        gpu.reset();
        out.passed[6] = true;
        note("Vulkan: fenced readbacks and cleanup completed");
    }
};
void consume(const OutputPacket &packet, unsigned index, void *opaque) noexcept
{
    auto &session = *static_cast<Session *>(opaque);
    try {
        session.packet(packet, index);
    } catch (const std::exception &error) {
        session.error(error.what());
    } catch (...) {
        session.error("unknown C++ exception");
    }
}
} // namespace
const char *outputGpuCheckLabel(unsigned i)
{
    return Labels[i];
}
OutputProbeResults runOutputWithGpuProbe(const FEXCore::HostFeatures &features,
                                         FEXCore::HLE::SyscallHandler *calls,
                                         void (*log)(const char *),
                                         OutputGpuResults &out) noexcept
{
    OutputProbeResults result;
    try {
        Session session{ out, log };
        result = runOutputProbe(features, calls, log, consume, &session);
        out.passed[0] = session.entries == 2;
        // Incomplete/failed CPU production must never silently pass GPU checks.
        if (!session.cleaned) {
            session.note("Vulkan: incomplete-session resource cleanup");
            if (session.gpu) {
                try {
                    session.gpu->finish();
                } catch (const std::exception &error) {
                    session.error(error.what());
                }
                session.gpu.reset();
            }
            if (!session.failed)
                session.error("both owned guest packets were not consumed");
        }
    } catch (const std::exception &error) {
        std::snprintf(out.detail, sizeof out.detail, "GPU bridge error: %s",
                      error.what());
    } catch (...) {
        std::snprintf(out.detail, sizeof out.detail,
                      "GPU bridge error: unknown C++ exception");
    }
    return result;
}
} // namespace armxbox
