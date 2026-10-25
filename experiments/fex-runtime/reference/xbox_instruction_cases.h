// Generated from reference/xbox-instructions.json; not FEX output.
#pragma once
#include <array>
#include <cstdint>
namespace armxbox {
struct InstructionCase {
    const char *name;
    std::array<uint8_t, 5> bytes;
    unsigned size;
    uint8_t trap;
};
inline constexpr InstructionCase XboxInstructionCases[] = {
    {"sse1_xorps", {0x0f, 0x57, 0xc0}, 3, 0},
    {"mmx_pxor", {0x0f, 0xef, 0xc0}, 3, 0},
    {"x87_fld1", {0xd9, 0xe8}, 2, 0},
    {"sse2_pxor", {0x66, 0x0f, 0xef, 0xc0}, 4, 0},
    {"sse3_haddps", {0xf2, 0x0f, 0x7c, 0xc0}, 4, 6},
    {"ssse3_pshufb", {0x66, 0x0f, 0x38, 0x00, 0xc0}, 5, 6},
    {"sse41_ptest", {0x66, 0x0f, 0x38, 0x17, 0xc0}, 5, 6},
    {"sse42_pcmpgtq", {0x66, 0x0f, 0x38, 0x37, 0xc0}, 5, 6},
    {"avx_vzeroupper", {0xc5, 0xf8, 0x77}, 3, 6},
    {"rdtscp", {0x0f, 0x01, 0xf9}, 3, 6},
    {"xgetbv", {0x0f, 0x01, 0xd0}, 3, 6},
    {"ud2", {0x0f, 0x0b}, 2, 6},
};
} // namespace armxbox
