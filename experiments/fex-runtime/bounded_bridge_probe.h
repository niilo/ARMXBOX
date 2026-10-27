#pragma once
#include <array>
#include <cstdint>
namespace FEXCore {
namespace Context {
class Context;
}
namespace Core {
struct InternalThreadState;
}
} // namespace FEXCore
namespace armxbox {
inline constexpr unsigned BridgeCheckCount = 16;
struct BoundedBridgeResults {
    std::array<bool, BridgeCheckCount> passed{};
    std::array<bool, 2> native{};
    uint64_t abiMask = 0x3f;
    unsigned entries = 0;
    uint64_t stopLatencyUs = 0;
    char detail[256]{};
};
const char *boundedBridgeCheckLabel(unsigned);
BoundedBridgeResults runBoundedBridgeProbe(FEXCore::Context::Context *,
                                           FEXCore::Core::InternalThreadState *,
                                           uintptr_t code, uintptr_t data,
                                           uintptr_t stack,
                                           void (*log)(const char *));
} // namespace armxbox
