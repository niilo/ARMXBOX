// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace FEXCore {
struct HostFeatures;
namespace HLE {
class SyscallHandler;
}
} // namespace FEXCore
namespace armxbox {
// Only the project-owned, unsigned generated profile is admitted. This is not
// a retail XBE loader or signature/authentication implementation.
constexpr uint32_t XbeBase = 0x00010000;
constexpr uint32_t XbeImageSize = 0x4000;
constexpr uint32_t XbeEntry = XbeBase + 0x1000;
constexpr uint32_t XbeImportStub = XbeBase + 0x1100;
constexpr uint32_t XbeThunk = XbeBase + 0x2000;
constexpr uint32_t XbeFill = XbeBase + 0x3000;
constexpr uint32_t XbeReturn = XbeEntry + 15;
constexpr uint32_t XbeCompletion = XbeEntry + 25;
using GeneratedXbe = std::array<uint8_t, 0x3000>;
GeneratedXbe generateControlledXbe();
struct XbePlan {
    uint32_t entry = 0, thunk = 0;
    std::array<uint32_t, 2> rawOffset{}, rawSize{}, virtualOffset{},
        virtualSize{};
};
bool admitControlledXbe(std::span<const uint8_t>, XbePlan &);
std::array<bool, 4> controlledXbeAdmissionControls();
constexpr unsigned XbeCheckCount = 15;
struct ControlledXbeResults {
    std::array<bool, XbeCheckCount> passed{};
    uint64_t abiMask = 0x3f;
    unsigned entries = 0;
    char detail[256]{};
};
const char *controlledXbeCheckLabel(unsigned);
ControlledXbeResults runControlledXbeProbe(const FEXCore::HostFeatures &,
                                           FEXCore::HLE::SyscallHandler *,
                                           void (*log)(const char *));
} // namespace armxbox
