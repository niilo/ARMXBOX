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
struct RawFaultResults {
    std::array<bool, 11> passed{};
    char detail[256]{};
};
RawFaultResults runRawFaultProbe(FEXCore::Context::Context *,
                                 FEXCore::Core::InternalThreadState *,
                                 uint64_t code, uint64_t guard,
                                 void (*execute)(void *), void *opaque);
} // namespace armxbox
