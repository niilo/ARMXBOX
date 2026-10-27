// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "output_probe.h"
namespace armxbox {
constexpr unsigned OutputGpuCheckCount = 7;
struct OutputGpuResults {
    std::array<bool, OutputGpuCheckCount> passed{};
    char detail[1024]{};
};
const char *outputGpuCheckLabel(unsigned);
// All C++ exceptions are caught within this exceptions-enabled translation
// unit.
OutputProbeResults runOutputWithGpuProbe(const FEXCore::HostFeatures &,
                                         FEXCore::HLE::SyscallHandler *,
                                         void (*log)(const char *),
                                         OutputGpuResults &) noexcept;
} // namespace armxbox
