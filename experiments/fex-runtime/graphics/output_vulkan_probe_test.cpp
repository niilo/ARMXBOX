// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the real exception boundary/orchestration with injected failures.
// The fake device is test-only; real Vulkan/pixel behavior has separate tests.
#include "output_vulkan_probe.h"
#include "output_packet.h"
#include "output_vulkan.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
namespace FEXCore {
struct HostFeatures {};
} // namespace FEXCore
namespace {
enum class Mode {
    Happy,
    Constructor,
    ColdRender,
    WarmRender,
    ColdPixels,
    WarmPixels,
    HostPixels,
    Finish,
    MissingSecond,
    WrongIndex
};
struct State {
    Mode mode{};
    unsigned constructors = 0, renders = 0, finishes = 0, destroyed = 0;
    unsigned producerCallbacks = 0, producerRetirements = 0;
    unsigned closedBeforeSecondRetirement = 0;
    const armxbox::OutputPacket *active = nullptr;
    bool closed = false, injected = false;
} state;
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
} // namespace
namespace armxbox {
struct OutputVulkan::Impl {
    ~Impl()
    {
        ++state.destroyed;
    }
};
OutputVulkan::OutputVulkan(std::span<const uint32_t> vertex,
                           std::span<const uint32_t> fragment, bool validation)
    : impl(std::make_unique<Impl>())
{
    ++state.constructors;
    require(
        !validation && vertex.size() >= 5 && fragment.size() >= 5 &&
            vertex.front() == 0x07230203 && fragment.front() == 0x07230203,
        "native bridge did not supply checked embedded shaders without layers");
    if (state.mode == Mode::Constructor) {
        state.injected = true;
        throw std::runtime_error("injected constructor failure");
    }
}
OutputVulkan::~OutputVulkan() = default;
OutputPixels OutputVulkan::render(const OutputPacket &packet)
{
    require(!state.closed, "render after cleanup");
    ++state.renders;
    if (packet.sequence() < 3)
        require(&packet == state.active &&
                    outputMatches(packet, packet.sequence()),
                "renderer used a retired or unexpected guest packet");
    else
        require(state.active && state.active->sequence() == 2,
                "host control ran outside second guest lease");
    if ((state.mode == Mode::ColdRender && state.renders == 1) ||
        (state.mode == Mode::WarmRender && state.renders == 2)) {
        state.injected = true;
        throw std::runtime_error("injected fenced render failure");
    }
    OutputPixels pixels{};
    pixels[0] = packet.texture()[0];
    if ((state.mode == Mode::ColdPixels && state.renders == 1) ||
        (state.mode == Mode::WarmPixels && state.renders == 2) ||
        (state.mode == Mode::HostPixels && state.renders == 3)) {
        state.injected = true;
        pixels[0] ^= 1;
    }
    return pixels;
}
const char *OutputVulkan::detail() const noexcept
{
    return "device=mock-native; driver=123; validation=0";
}
void OutputVulkan::finish()
{
    ++state.finishes;
    if (state.mode == Mode::Finish) {
        state.injected = true;
        throw std::runtime_error("injected cleanup failure");
    }
    // The successful path must finish while the producer still owns lease two.
    if (state.renders == 3) {
        require(state.active && state.active->sequence() == 2,
                "GPU finish occurred after second guest lease retirement");
        ++state.closedBeforeSecondRetirement;
    }
    state.closed = true;
}
unsigned outputPixelMismatches(const OutputPixels &pixels,
                               const OutputPacket &packet)
{
    return pixels[0] != packet.texture()[0];
}
OutputProbeResults runOutputProbe(const FEXCore::HostFeatures &,
                                  FEXCore::HLE::SyscallHandler *,
                                  void (*)(const char *),
                                  OutputPacketConsumer consume, void *opaque)
{
    OutputProbeResults result;
    result.passed.fill(true);
    result.entries = 4;
    result.abiMask = 0x3f;
    OutputQueue queue;
    for (unsigned serial = 1; serial <= 2; ++serial) {
        auto source = outputFixture(0x13000, serial);
        require(queue.submit({ 0x13000, source, 1, true, false }, 0x13000) ==
                    OutputError::None,
                "fake producer packet capture");
        const auto lease = queue.acquire();
        const auto *packet = queue.get(lease);
        require(packet && consume, "fake producer lease/callback");
        source.fill(0x39);
        state.active = packet;
        if (serial == 1 || state.mode != Mode::MissingSecond) {
            ++state.producerCallbacks;
            consume(*packet, state.mode == Mode::WrongIndex ? 1 : serial - 1,
                    opaque);
        }
        require(queue.get(lease) == packet, "consumer retired producer lease");
        state.active = nullptr;
        require(queue.retire(lease) && !queue.get(lease),
                "producer retirement");
        ++state.producerRetirements;
    }
    return result;
}
} // namespace armxbox
int main()
{
    try {
        constexpr Mode modes[] = { Mode::Happy,         Mode::Constructor,
                                   Mode::ColdRender,    Mode::WarmRender,
                                   Mode::ColdPixels,    Mode::WarmPixels,
                                   Mode::HostPixels,    Mode::Finish,
                                   Mode::MissingSecond, Mode::WrongIndex };
        constexpr const char *Names[] = {
            "happy reusable device",       "constructor exception",
            "cold render exception",       "warm render exception",
            "cold pixel mismatch",         "warm pixel mismatch",
            "host control pixel mismatch", "cleanup exception",
            "missing second packet",       "unexpected packet order"
        };
        for (const auto mode : modes) {
            state = {};
            state.mode = mode;
            armxbox::OutputGpuResults gpu;
            const auto cpu = armxbox::runOutputWithGpuProbe(
                FEXCore::HostFeatures{}, nullptr, nullptr, gpu);
            require(std::all_of(cpu.passed.begin(), cpu.passed.end(),
                                [](bool pass) { return pass; }) &&
                        cpu.entries == 4 && cpu.abiMask == 0x3f,
                    "GPU failure changed inherited CPU outcomes");
            require(state.producerRetirements == 2 && !state.active,
                    "GPU path disrupted producer lease cleanup");
            const bool all = std::all_of(gpu.passed.begin(), gpu.passed.end(),
                                         [](bool pass) { return pass; });
            require(all == (mode == Mode::Happy),
                    "incorrect GPU result admission");
            if (mode == Mode::Happy) {
                require(
                    state.constructors == 1 && state.renders == 3 &&
                        state.finishes == 1 && state.destroyed == 1 &&
                        state.closedBeforeSecondRetirement == 1,
                    "happy orchestration did not reuse and finish one device");
            } else if (mode == Mode::MissingSecond) {
                require(!gpu.passed[0] && gpu.passed[1] && gpu.passed[2] &&
                            !gpu.passed[3] && !gpu.passed[6] &&
                            state.finishes == 1,
                        "missing second packet did not fail and clean up");
            } else if (mode == Mode::WrongIndex) {
                require(state.constructors == 0 && !gpu.passed[0],
                        "unexpected producer order reached the device");
            } else {
                require(
                    state.injected && state.destroyed == 1,
                    "failure injection not exercised or device owner leaked");
                if (mode == Mode::Constructor)
                    require(state.renders == 0 && !gpu.passed[1],
                            "constructor failure admitted device work");
                if (mode == Mode::ColdRender)
                    require(state.renders == 1 && !gpu.passed[2] &&
                                !gpu.passed[3],
                            "cold failure allowed later GPU entry");
                if (mode == Mode::WarmRender)
                    require(
                        state.renders == 2 && gpu.passed[2] && !gpu.passed[3],
                        "warm failure erased cold result or allowed host work");
                if (mode == Mode::ColdPixels)
                    require(!gpu.passed[2] && gpu.passed[3] && !gpu.passed[4],
                            "cold pixel mismatch was hidden");
                if (mode == Mode::WarmPixels)
                    require(gpu.passed[2] && !gpu.passed[3] && !gpu.passed[4],
                            "warm pixel mismatch was hidden");
                if (mode == Mode::HostPixels)
                    require(gpu.passed[2] && gpu.passed[3] && !gpu.passed[5],
                            "host pixel mismatch changed guest results");
                if (mode == Mode::Finish)
                    require(gpu.passed[2] && gpu.passed[3] && !gpu.passed[6],
                            "cleanup failure was admitted");
                if (mode != Mode::Constructor && mode != Mode::ColdPixels &&
                    mode != Mode::WarmPixels && mode != Mode::HostPixels)
                    require(std::strstr(gpu.detail, "driver=123"),
                            "failure diagnostics lost the device identity");
            }
            std::printf(
                "PASS GPU bridge %s: CPU outcomes and leases retained\n",
                Names[unsigned(mode)]);
        }
        std::puts("PASS GPU bridge: 10 orchestration/exception controls; all "
                  "12 CPU outcomes retained");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL GPU bridge test: %s\n", error.what());
        return 1;
    }
}
