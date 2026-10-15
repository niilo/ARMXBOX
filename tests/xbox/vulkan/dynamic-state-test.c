/*
 * Vulkan dynamic-state command cache regression fixture.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ctz32 __builtin_ctz
#include "hw/xbox/nv2a/nv2a_regs.h"
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#ifndef OPT_DYNAMIC_BLEND
#define OPT_DYNAMIC_BLEND 1
#endif
#define VK_TRUE 1
#define VK_FALSE 0
#define VK_STENCIL_FACE_FRONT_AND_BACK 3
#define VK_COLOR_COMPONENT_R_BIT 1
#define VK_COLOR_COMPONENT_G_BIT 2
#define VK_COLOR_COMPONENT_B_BIT 4
#define VK_COLOR_COMPONENT_A_BIT 8
typedef uint32_t VkCullModeFlags;
typedef uint32_t VkCompareOp;
typedef uint32_t VkStencilOp;
typedef uint32_t VkBlendFactor;
typedef uint32_t VkBlendOp;
typedef uint32_t VkBool32;
typedef uint32_t VkColorComponentFlags;
typedef struct VkColorBlendEquationEXT {
    uint32_t srcColorBlendFactor, dstColorBlendFactor, colorBlendOp;
    uint32_t srcAlphaBlendFactor, dstAlphaBlendFactor, alphaBlendOp;
} VkColorBlendEquationEXT;
enum { CULL, FRONT, CONSTANTS, DEPTH_TEST, DEPTH_WRITE, DEPTH_OP,
       STENCIL_WRITE, STENCIL_TEST, STENCIL_COMPARE, STENCIL_REF, STENCIL_OP,
       BLEND_ENABLE, BLEND_EQ, COLOR_MASK, NUM_STATE };
typedef struct Gpu {
    uint32_t state[NUM_STATE][6];
    uint64_t commands;
} Gpu;
typedef struct PGRAPHState {
    uint32_t regs[0x2000 / 4];
    bool clearing;
} PGRAPHState;
typedef struct PGRAPHVkState {
    bool extended_dynamic_state_supported, eds3_blend_supported;
    Gpu *command_buffer;
#include "dynamic-state-types.inc"
} PGRAPHVkState;
static uint32_t pgraph_vk_reg_r(PGRAPHState *pg, uint32_t reg)
{
    return pg->regs[reg / 4];
}

static void pgraph_argb_pack32_to_rgba_float(uint32_t c, float *f)
{
    f[0] = ((c >> 16) & 255) / 255.0f;
    f[1] = ((c >> 8) & 255) / 255.0f;
    f[2] = (c & 255) / 255.0f;
    f[3] = (c >> 24) / 255.0f;
}

static void emit1(Gpu *g, int slot, uint32_t value)
{
    g->state[slot][0] = value;
    g->commands++;
}

#define SIMPLE(name, slot)                         \
    static void name(Gpu *g, uint32_t a)            \
    {                                             \
        emit1(g, slot, a);                         \
    }
SIMPLE(vkCmdSetCullMode, CULL)
SIMPLE(vkCmdSetFrontFace, FRONT)
SIMPLE(vkCmdSetDepthTestEnable, DEPTH_TEST)
SIMPLE(vkCmdSetDepthWriteEnable, DEPTH_WRITE)
SIMPLE(vkCmdSetDepthCompareOp, DEPTH_OP)
SIMPLE(vkCmdSetStencilTestEnable, STENCIL_TEST)
#define STENCIL(name, slot)                                    \
    static void name(Gpu *g, uint32_t f, uint32_t a)             \
    {                                                         \
        assert(f == VK_STENCIL_FACE_FRONT_AND_BACK);            \
        emit1(g, slot, a);                                     \
    }
STENCIL(vkCmdSetStencilWriteMask, STENCIL_WRITE)
STENCIL(vkCmdSetStencilCompareMask, STENCIL_COMPARE)
STENCIL(vkCmdSetStencilReference, STENCIL_REF)
static void vkCmdSetBlendConstants(Gpu *g, const float *a)
{
    memcpy(g->state[CONSTANTS], a, 4 * sizeof(*a));
    g->commands++;
}

static void vkCmdSetStencilOp(Gpu *g, uint32_t f, uint32_t a, uint32_t b,
                              uint32_t c, uint32_t d)
{
    uint32_t v[] = { a, b, c, d };

    assert(f == VK_STENCIL_FACE_FRONT_AND_BACK);
    memcpy(g->state[STENCIL_OP], v, sizeof(v));
    g->commands++;
}

#if OPT_DYNAMIC_BLEND
static void vkCmdSetColorBlendEnableEXT(Gpu *g, uint32_t s, uint32_t n,
                                        const VkBool32 *a)
{
    assert(s == 0 && n == 1);
    emit1(g, BLEND_ENABLE, *a);
}

static void vkCmdSetColorWriteMaskEXT(Gpu *g, uint32_t s, uint32_t n,
                                      const VkColorComponentFlags *a)
{
    assert(s == 0 && n == 1);
    emit1(g, COLOR_MASK, *a);
}

static void vkCmdSetColorBlendEquationEXT(Gpu *g, uint32_t s, uint32_t n,
                                         const VkColorBlendEquationEXT *a)
{
    assert(s == 0 && n == 1);
    memcpy(g->state[BLEND_EQ], a, sizeof(*a));
    g->commands++;
}

#endif
#include "dynamic-state-functions.inc"
static uint64_t seed = 0x583d21af;
static uint32_t rng(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
}

static void normalize(PGRAPHState *pg)
{
    uint32_t *c0 = &pg->regs[NV_PGRAPH_CONTROL_0 / 4];
    uint32_t *c1 = &pg->regs[NV_PGRAPH_CONTROL_1 / 4];
    uint32_t *c2 = &pg->regs[NV_PGRAPH_CONTROL_2 / 4];

    SET_MASK(*c0, NV_PGRAPH_CONTROL_0_ZFUNC, rng() & 7);
    SET_MASK(*c1, NV_PGRAPH_CONTROL_1_STENCIL_FUNC, rng() & 7);
    SET_MASK(*c2, NV_PGRAPH_CONTROL_2_STENCIL_OP_FAIL, rng() % 9);
    SET_MASK(*c2, NV_PGRAPH_CONTROL_2_STENCIL_OP_ZFAIL, rng() % 9);
    SET_MASK(*c2, NV_PGRAPH_CONTROL_2_STENCIL_OP_ZPASS, rng() % 9);
    SET_MASK(pg->regs[NV_PGRAPH_BLEND / 4], NV_PGRAPH_BLEND_EQN, rng() % 7);
}

static const uint32_t registers[] = {
    NV_PGRAPH_CONTROL_0, NV_PGRAPH_CONTROL_1, NV_PGRAPH_CONTROL_2,
    NV_PGRAPH_BLEND, NV_PGRAPH_BLENDCOLOR, NV_PGRAPH_SETUPRASTER,
};
static bool sequence(bool eds, bool blend, unsigned steps, bool negative,
                     uint64_t *old_count, uint64_t *new_count)
{
    Gpu a = { 0 }, b = { 0 };
    PGRAPHVkState old = {
        .command_buffer = &a,
        .extended_dynamic_state_supported = eds,
        .eds3_blend_supported = blend,
    };
    PGRAPHVkState proposed = old;
    proposed.command_buffer = &b;
    PGRAPHState pg = { 0 };
    for (unsigned i = 0; i < steps; i++) {
        pg.clearing = i % 31 == 30;
        if (i % 47 == 0) {
            memset(a.state, 0xa5, sizeof(a.state));
            memset(b.state, 0xa5, sizeof(b.state));
            old.dyn_state.valid = proposed.dyn_state.valid = false;
        }
        if (i % 11 == 10) {
            /* Repeated unchanged state must not emit extra commands. */
        } else if (i % 7 == 0) {
            for (unsigned n = 0; n < ARRAY_SIZE(registers); n++) {
                pg.regs[registers[n] / 4] = rng();
            }
            normalize(&pg);
        } else {
            unsigned n = rng() % ARRAY_SIZE(registers);
            uint32_t bit = UINT32_C(1) << (rng() % 32);
            pg.regs[registers[n] / 4] ^= bit;
            /* Keep map indices valid without overwriting independent words. */
            pg.regs[NV_PGRAPH_CONTROL_0 / 4] &= ~(UINT32_C(1) << 19);
            pg.regs[NV_PGRAPH_CONTROL_1 / 4] &= ~(UINT32_C(1) << 7);
        }
        uint32_t *c2 = &pg.regs[NV_PGRAPH_CONTROL_2 / 4];
        SET_MASK(*c2, NV_PGRAPH_CONTROL_2_STENCIL_OP_FAIL,
                 GET_MASK(*c2, NV_PGRAPH_CONTROL_2_STENCIL_OP_FAIL) % 9);
        SET_MASK(*c2, NV_PGRAPH_CONTROL_2_STENCIL_OP_ZFAIL,
                 GET_MASK(*c2, NV_PGRAPH_CONTROL_2_STENCIL_OP_ZFAIL) % 9);
        SET_MASK(*c2, NV_PGRAPH_CONTROL_2_STENCIL_OP_ZPASS,
                 GET_MASK(*c2, NV_PGRAPH_CONTROL_2_STENCIL_OP_ZPASS) % 9);
        uint32_t *bl = &pg.regs[NV_PGRAPH_BLEND / 4];
        SET_MASK(*bl, NV_PGRAPH_BLEND_EQN,
                 GET_MASK(*bl, NV_PGRAPH_BLEND_EQN) % 7);
        old_update(&pg, &old);
        if (negative) {
            negative_update(&pg, &proposed);
        } else {
            new_update(&pg, &proposed);
        }
        if (memcmp(a.state, b.state, sizeof(a.state))) {
            if (!negative) {
                fprintf(stderr, "mismatch step=%u eds=%d blend=%d\n",
                        i, eds, blend);
            }
            return false;
        }
    }
    *old_count = a.commands;
    *new_count = b.commands;
    return true;
}

static void single_bits(void)
{
    unsigned cases = 0;

    for (unsigned eds = 0; eds < 2; eds++) {
        for (unsigned blend = 0; blend < 2; blend++) {
            for (unsigned reg = 0; reg < ARRAY_SIZE(registers); reg++) {
                for (unsigned bit = 0; bit < 32; bit++) {
                    PGRAPHState pg = { 0 };
                    uint32_t value = UINT32_C(1) << bit;
                    if (registers[reg] == NV_PGRAPH_CONTROL_0 && bit == 19) {
                        continue;
                    }
                    if (registers[reg] == NV_PGRAPH_CONTROL_1 && bit == 7) {
                        continue;
                    }
                    Gpu a = { 0 }, b = { 0 };
                    PGRAPHVkState old = {
                        .command_buffer = &a,
                        .extended_dynamic_state_supported = eds,
                        .eds3_blend_supported = blend,
                    };
                    PGRAPHVkState proposed = old;
                    proposed.command_buffer = &b;
                    old_update(&pg, &old);
                    new_update(&pg, &proposed);
                    pg.regs[registers[reg] / 4] = value;
                    old_update(&pg, &old);
                    new_update(&pg, &proposed);
                    assert(!memcmp(a.state, b.state, sizeof(a.state)));
                    pg.regs[registers[reg] / 4] = 0;
                    old_update(&pg, &old);
                    new_update(&pg, &proposed);
                    assert(!memcmp(a.state, b.state, sizeof(a.state)));
                    cases++;
                }
            }
        }
    }
    printf("%u explicit valid single-bit set/clear cases pass\n", cases);
}

static bool performance_control;

static void edge(const char *name, uint32_t reg, uint32_t bit,
                 unsigned expected_old, unsigned expected_new)
{
    Gpu a = { 0 }, b = { 0 };
    PGRAPHVkState old = {
        .command_buffer = &a,
        .extended_dynamic_state_supported = true,
        .eds3_blend_supported = true,
    };
    PGRAPHVkState proposed = old;
    proposed.command_buffer = &b;
    PGRAPHState pg = { 0 };
    old_update(&pg, &old);
    new_update(&pg, &proposed);
    a.commands = b.commands = 0;
    pg.regs[reg / 4] ^= bit;
    old_update(&pg, &old);
    if (performance_control) {
        old_update(&pg, &proposed);
    } else {
        new_update(&pg, &proposed);
    }
    assert(!memcmp(a.state, b.state, sizeof(a.state)));
    if (a.commands != expected_old || b.commands != expected_new) {
        fprintf(stderr, "redundant dynamic command survived: %s\n", name);
        exit(1);
    }
    printf("%s commands: old=%llu masked=%llu\n", name,
           (unsigned long long)a.commands, (unsigned long long)b.commands);
}

int main(int argc, char **argv)
{
    uint64_t dummy = 0;

    performance_control = argc == 2 && !strcmp(argv[1], "baseline-control");
    if (!performance_control) {
        for (unsigned eds = 0; eds < 2; eds++) {
            for (unsigned blend = 0; blend < 2; blend++) {
                uint64_t old_count, new_count;
                assert(sequence(eds, blend, 250000, false,
                                &old_count, &new_count));
                printf("EDS%u blend%u: 250000 transitions pass, "
                       "commands=%llu -> %llu\n",
                       eds, blend, (unsigned long long)old_count,
                       (unsigned long long)new_count);
            }
        }
        single_bits();
        assert(!sequence(true, true, 250000, true, &dummy, &dummy));
        puts("Negative control omitting stencil-write-enable dependency: "
             "detected");
    }
    edge("alpha-reference-only", NV_PGRAPH_CONTROL_0, 1,
         4 + 3 * OPT_DYNAMIC_BLEND, 0);
    edge("color-write-only", NV_PGRAPH_CONTROL_0,
         NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE, 4 + 3 * OPT_DYNAMIC_BLEND,
         3 * OPT_DYNAMIC_BLEND);
    edge("depth-write-only", NV_PGRAPH_CONTROL_0,
         NV_PGRAPH_CONTROL_0_ZWRITEENABLE, 4 + 3 * OPT_DYNAMIC_BLEND, 4);
    edge("logic-op-only", NV_PGRAPH_BLEND, NV_PGRAPH_BLEND_LOGICOP_ENABLE,
         3 * OPT_DYNAMIC_BLEND, 0);
    edge("stencil-ignored-bit", NV_PGRAPH_CONTROL_1, 2, 5, 0);
    edge("setup-polygon-only", NV_PGRAPH_SETUPRASTER, 1, 2, 0);
    return 0;
}
