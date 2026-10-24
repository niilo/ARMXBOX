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
struct PCMetadataResults {
    std::array<bool, 6> passed{};
    std::array<bool, 6> flags{};
    char detail[2048]{};
};
PCMetadataResults runPCMetadataProbe(FEXCore::Context::Context *,
                                     FEXCore::Core::InternalThreadState *,
                                     uint64_t code, uint64_t data,
                                     void (*execute)(void *), void *opaque);
} // namespace armxbox
