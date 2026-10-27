// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
namespace FEXCore {
struct HostFeatures;
namespace HLE {
class SyscallHandler;
}
} // namespace FEXCore
namespace armxbox {
class OutputPacket;
using OutputPacketConsumer = void (*)(const OutputPacket &, unsigned,
                                      void *) noexcept;
constexpr unsigned OutputCheckCount = 12;
struct OutputProbeResults {
    std::array<bool, OutputCheckCount> passed{};
    uint64_t abiMask = 0x3f;
    unsigned entries = 0;
    char detail[256]{};
};
const char *outputProbeCheckLabel(unsigned);
OutputProbeResults runOutputProbe(const FEXCore::HostFeatures &,
                                  FEXCore::HLE::SyscallHandler *,
                                  void (*log)(const char *),
                                  OutputPacketConsumer consume = nullptr,
                                  void *consumer = nullptr);
} // namespace armxbox
