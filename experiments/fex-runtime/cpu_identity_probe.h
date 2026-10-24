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
struct CPUIdentityResults {
    std::array<bool, 12> passed{};
    uint64_t abiMask = 0x3f;
    unsigned nativeEntries = 0;
    char detail[256]{};
};
CPUIdentityResults runCPUIdentityProbe(const FEXCore::HostFeatures &,
                                       FEXCore::HLE::SyscallHandler *,
                                       uintptr_t code, uintptr_t data,
                                       uintptr_t stack);
} // namespace armxbox
