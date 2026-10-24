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
inline constexpr unsigned ExecutionCheckCount = 15;
struct ExecutionControlResults {
    std::array<bool, ExecutionCheckCount> passed{};
    std::array<bool, 4> native{};
    uint64_t abiMask = 0x3f;
    unsigned entries = 0;
    uint64_t stopLatencyUs[2]{};
    char detail[512]{};
};
const char *executionCheckLabel(unsigned);
ExecutionControlResults runExecutionControlProbe(const FEXCore::HostFeatures &,
                                                 FEXCore::HLE::SyscallHandler *,
                                                 uintptr_t code, uintptr_t data,
                                                 uintptr_t stack,
                                                 void (*log)(const char *));
} // namespace armxbox
