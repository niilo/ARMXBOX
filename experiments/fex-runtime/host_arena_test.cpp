#include "host_arena_probe.h"
#include <cstdio>
#include <sys/mman.h>
int main()
{
    constexpr uintptr_t Code = 0x10000000;
    constexpr size_t Size = 65536;
    void *p = mmap(reinterpret_cast<void *>(Code), Size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED || reinterpret_cast<uintptr_t>(p) != Code) {
        if (p != MAP_FAILED) {
            munmap(p, Size);
        }
        std::fprintf(stderr, "FAIL arena test low fixture reservation\n");
        return 1;
    }
    const auto result = armxbox::prepareHostArena(
        Code, [](const char *text) { std::fprintf(stderr, "%s\n", text); });
    unsigned failures = 0;
    for (unsigned i = 0; i < result.passed.size(); ++i) {
        std::printf("%s host arena control %u\n",
                    result.passed[i] ? "PASS" : "FAIL", i);
        failures += !result.passed[i];
    }
    if (result.detail[0]) {
        std::fprintf(stderr, "%s\n", result.detail);
    }
    if (result.ready) {
        const auto after = armxbox::finishHostArena(result.base, result.size);
        for (bool good : after) {
            failures += !good;
        }
    } else {
        failures += 2;
    }
    munmap(p, Size);
    std::printf("%s host arena isolated process: 20 checks\n",
                failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
