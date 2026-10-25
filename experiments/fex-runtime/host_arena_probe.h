#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace armxbox {
constexpr size_t ArenaCheckCount = 18;
struct HostArenaResults {
    bool ready = false;
    uintptr_t base = 0;
    size_t size = 2ULL << 30;
    std::array<bool, ArenaCheckCount> passed{};
    char detail[512]{};
};
struct HostMemorySnapshot {
    bool available = false;
    size_t virtualKiB = 0, residentKiB = 0;
};
HostMemorySnapshot hostMemorySnapshot();
HostArenaResults prepareHostArena(uintptr_t guestCode,
                                  void (*log)(const char *) = nullptr);
std::array<bool, 2> finishHostArena(uintptr_t base, size_t size);
} // namespace armxbox
