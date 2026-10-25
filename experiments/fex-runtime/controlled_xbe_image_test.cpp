#include "controlled_xbe.h"
#include <cstdio>
int main(int argc, char **argv)
{
    const auto checks = armxbox::controlledXbeAdmissionControls();
    for (unsigned i = 0; i < checks.size(); ++i) {
        std::printf("%s XBE image admission group %u\n",
                    checks[i] ? "PASS" : "FAIL", i);
        if (!checks[i])
            return 1;
    }
    // Seeded mutations vary both contents and every admitted file length.
    // Accepted padding changes must still produce the exact bounded plan.
    uint32_t seed = 0x4a11c02;
    for (unsigned i = 0; i < 4000; ++i) {
        auto bytes = armxbox::generateControlledXbe();
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        bytes[seed % bytes.size()] ^= uint8_t((seed >> 16) | 1);
        const size_t length = i % 2 ? bytes.size() : seed % bytes.size();
        armxbox::XbePlan plan;
        if (armxbox::admitControlledXbe(std::span(bytes).first(length), plan)) {
            if (plan.entry != armxbox::XbeEntry ||
                plan.thunk != armxbox::XbeThunk ||
                plan.virtualOffset[0] != 4096 ||
                plan.virtualOffset[1] != 8192 || plan.virtualSize[0] != 4096 ||
                plan.virtualSize[1] != 8192)
                return 1;
        } else if (plan.entry || plan.thunk)
            return 1;
    }
    std::puts("PASS XBE seeded mutation and bounded-plan controls: 4000 cases");
    if (argc == 2) {
        const auto bytes = armxbox::generateControlledXbe();
        FILE *f = std::fopen(argv[1], "wb");
        if (!f)
            return 1;
        const auto count = std::fwrite(bytes.data(), 1, bytes.size(), f);
        const int close = std::fclose(f);
        if (count != bytes.size() || close)
            return 1;
    }
}
