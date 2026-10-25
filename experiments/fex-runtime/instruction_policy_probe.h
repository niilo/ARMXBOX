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
inline constexpr unsigned InstructionPolicyCheckCount = 27;
struct InstructionPolicyResults {
    std::array<bool, InstructionPolicyCheckCount> passed{};
    uint64_t abiMask = 0x3f;
    unsigned entries = 0;
    char detail[512]{};
};
const char *instructionPolicyCheckLabel(unsigned);
InstructionPolicyResults runInstructionPolicyProbe(
    const FEXCore::HostFeatures &, FEXCore::HLE::SyscallHandler *,
    uintptr_t code, uintptr_t data, uintptr_t stack, void (*log)(const char *));
} // namespace armxbox
