/*
 * Setter/getter and cache-dispatch freshness regression.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define main primitive_cache_test_main
#include "uniform-freshness-base.inc"
#undef main
#include <stdlib.h>

#define QEMU_BUILD_BUG_MSG(condition, message) \
    _Static_assert(!(condition), message)
#define NV2A_UNIMPLEMENTED(...) assert(!"unexpected W-buffer mode")

static uint8_t pgraph_reg_category_table[0x2000 / 4];
static uint32_t pgraph_reg_dynamic_mask_table[0x2000 / 4];
static void bitmap_set(uint64_t *bits, unsigned start, unsigned count)
{
    assert(count == 1);
    bits[start / 64] |= UINT64_C(1) << (start % 64);
}

enum {
    VshUniform_material_alpha, VshUniform_lightInfiniteHalfVector,
    VshUniform_lightInfiniteDirection, VshUniform_lightLocalPosition,
    VshUniform_lightLocalAttenuation,
};
enum { PshUniform_alphaRef, PshUniform_depthOffset, PshUniform_depthFactor };
typedef struct VshUniformValues {
    float material_alpha[1];
    float lightInfiniteHalfVector[8][3], lightInfiniteDirection[8][3];
    float lightLocalPosition[8][3], lightLocalAttenuation[8][3];
} VshUniformValues;
typedef struct PshUniformValues {
    int alphaRef[1];
    float depthOffset[1], depthFactor[1];
} PshUniformValues;
#include "uniform-freshness-functions.inc"

static unsigned target, light, component, scenarios;
static bool alpha_uniform, depth_uniform;
static float serialized_factor;
static float actual_live_uniform(PGRAPHState *state)
{
    if (depth_uniform) {
        PshUniformValues values = {0};
        get_depth(state, &values);
        serialized_factor = values.depthFactor[0];
        return values.depthOffset[0];
    }
    if (alpha_uniform) {
        PshUniformValues values = {0};
        get_alpha(state, &values);
        return values.alphaRef[0];
    }
    VshUniformValues values = {0};
    get_lighting(state, &values);
    switch (target) {
    case 0: return values.material_alpha[0];
    case 1: return values.lightInfiniteHalfVector[light][component];
    case 2: return values.lightInfiniteDirection[light][component];
    case 3: return values.lightLocalPosition[light][component];
    case 4: return values.lightLocalAttenuation[light][component];
    default:
        abort();
    }
}

static void reset_uniform(void)
{
    reset(PRIM_TYPE_TRIANGLE_STRIP, POLY_MODE_FILL);
    alpha_uniform = false;
    depth_uniform = false;
    uniform_updates = pipeline_fallbacks = 0;
    allocation_uniform = 0;
    assert(guard_3(&pg));
}

static void set_nonregister(uint32_t value)
{
    static const unsigned methods[] = {
        0, NV097_SET_LIGHT_INFINITE_HALF_VECTOR,
        NV097_SET_LIGHT_INFINITE_DIRECTION, NV097_SET_LIGHT_LOCAL_POSITION,
        NV097_SET_LIGHT_LOCAL_ATTENUATION,
    };
    if (!target) {
        set_SET_MATERIAL_ALPHA(&pg, 0, value);
    } else {
        unsigned method = methods[target] + light * 128 + component * 4;
        set_SET_LIGHT_AMBIENT_COLOR(&pg, method, value);
    }
}

static void nonregister_values(void)
{
    static const uint32_t values[] = {
        0x3f800000, 0x80000000, 0x00000000, 0x7fc00001, 0x7fc00002,
    };
    for (target = 0; target < 5; target++) {
        for (light = 0; light < (target ? 8 : 1); light++) {
            for (component = 0; component < (target ? 3 : 1); component++) {
                reset_uniform();
                for (unsigned v = 0; v < ARRAY_SIZE(values); v++) {
                    set_nonregister(values[v]);
                    assert(pg.any_reg_gen == 0 && pg.shader_state_gen == 0);
                    assert(pg.non_dynamic_reg_gen == 0);
                    if (guard_3(&pg)) {
                        fputs("nonregister value escaped SFP\n", stderr);
                        abort();
                    }
                    assert(!cache_guard(4));
                    unsigned previous = uniform_updates;
                    pipeline_fast_hit(&pg);
                    assert(uniform_updates == previous + 1);
                    assert(!memcmp(&allocation_uniform, &values[v], 4));
                    assert(!pg.ltctxa_any_dirty && guard_3(&pg));
                    set_nonregister(values[v]);
                    assert(!pg.ltctxa_any_dirty && guard_3(&pg));
                    pipeline_fast_hit(&pg);
                    assert(uniform_updates == previous + 1);
                    pg.ltctxa_any_dirty = true;
                    set_nonregister(values[v]);
                    assert(pg.ltctxa_any_dirty);
                    consume_constant_flags(&pg);
                    scenarios++;
                }
            }
        }
    }
}

static void register_values(void)
{
    for (unsigned ended_pass = 0; ended_pass < 2; ended_pass++) {
        reset_uniform();
        alpha_uniform = true;
        pgraph_init_reg_category_table();
        pgraph_init_reg_dynamic_masks(true, true);
        set_SET_ALPHA_REF(&pg, 0, 43);
        assert(pg.any_reg_gen == 1 && pg.shader_state_gen == 0);
        assert(pg.pipeline_state_gen == 0 && pg.non_dynamic_reg_gen == 0);
        assert(!pg.vsh_constants_any_dirty && !r.uniforms_changed);
        if (ended_pass) {
            /* Nondraw commands can end a pass without retiring its buffer. */
            r.in_render_pass = false;
        } else {
            r.descriptor_set_index = r.ubo_descriptor_set_index = 0;
            r.push_ubo_set_index = 0;
        }
        assert(!guard_3(&pg) && !guard_5(&pg));
        pipeline_fast_hit(&pg);
        if (uniform_updates != 1 || allocation_uniform != 43) {
            fputs("register uniform stayed stale\n", stderr);
            abort();
        }
        assert(pipeline_fallbacks == 0);
        assert(r.last_any_reg_gen == 0);
        /* Uniform-only register without a shader/pipeline category. */
        set_SET_COMBINER_FACTOR0(&pg, NV097_SET_COMBINER_FACTOR0, 0xff112233);
        assert(pg.any_reg_gen == 2 && pg.non_dynamic_reg_gen == 1);
        pipeline_fast_hit(&pg);
        assert(uniform_updates == 2 && pipeline_fallbacks == 0);
        scenarios += 2;
    }
}

static void primitive_offsets(void)
{
    for (unsigned poly = POLY_MODE_POINT; poly <= POLY_MODE_LINE; poly++) {
        reset_uniform();
        reset(PRIM_TYPE_QUADS, poly);
        depth_uniform = true;
        pg.regs[NV_PGRAPH_SETUPRASTER] |=
            NV_PGRAPH_SETUPRASTER_POFFSETPOINTENABLE |
            NV_PGRAPH_SETUPRASTER_POFFSETLINEENABLE;
        pg.regs[NV_PGRAPH_ZOFFSETBIAS] = 0x40000000;
        pg.regs[NV_PGRAPH_ZOFFSETFACTOR] = 0x40400000;
        pgraph_vk_update_shader_uniforms(&pg);
        assert(allocation_uniform == 2 && serialized_factor == 3);
        unsigned previous = uniform_updates;
        pg.primitive_mode = poly == POLY_MODE_POINT ? PRIM_TYPE_POINTS :
                                                    PRIM_TYPE_LINES;
        assert(pgraph_glsl_primitive_mode_matches(&pg, &shader.state.geom));
        assert(pg.any_reg_gen == 0 && pg.vertex_attr_gen == 0);
        if (guard_3(&pg)) {
            fputs("primitive offset escaped SFP\n", stderr);
            abort();
        }
        assert(!cache_guard(4));
        pipeline_fast_hit(&pg);
        assert(uniform_updates == previous + 1 && pipeline_fallbacks == 0);
        assert(allocation_uniform == 0 && serialized_factor == 0);
        assert(guard_3(&pg));
        pg.primitive_mode = PRIM_TYPE_QUADS;
        assert(!guard_3(&pg));
        pipeline_fast_hit(&pg);
        assert(uniform_updates == previous + 2);
        assert(allocation_uniform == 2 && serialized_factor == 3);
        pg.primitive_mode = PRIM_TYPE_QUAD_STRIP;
        assert(guard_3(&pg));
        pipeline_fast_hit(&pg);
        assert(uniform_updates == previous + 2);
        scenarios += 3;
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "nonregister") || !strcmp(argv[1], "all")) {
        nonregister_values();
    }
    if (!strcmp(argv[1], "register") || !strcmp(argv[1], "all")) {
        register_values();
    }
    if (!strcmp(argv[1], "primitive") || !strcmp(argv[1], "all")) {
        primitive_offsets();
    }
    printf("uniform freshness: %u setter/getter/dispatch cases passed "
           "(bindless compiled=%d)\n", scenarios, OPT_BINDLESS_TEXTURES);
    return 0;
}
