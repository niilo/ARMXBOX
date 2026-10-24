#pragma once
#include <array>
#include <cstdint>
namespace FEXCore::Context {
class Context;
}
namespace FEXCore::Core {
struct InternalThreadState;
}
namespace armxbox {
inline constexpr unsigned PrivilegedExitCount = 19;
struct PrivilegedExitResults {
    std::array<bool, PrivilegedExitCount> passed{};
    uint64_t abiMask = 0x3f;
    unsigned entries = 0;
    bool canonicalFlags = true;
    char detail[2048]{};
};
const char *privilegedExitLabel(unsigned index);
PrivilegedExitResults
runPrivilegedExitProbe(FEXCore::Context::Context *,
                       FEXCore::Core::InternalThreadState *, uintptr_t code,
                       uintptr_t data, uintptr_t stack, uintptr_t guard);
} // namespace armxbox
