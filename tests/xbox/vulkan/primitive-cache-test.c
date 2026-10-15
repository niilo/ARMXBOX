/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Actual geometry builder/checker and draw guards, without Vulkan. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define OPT_STAT_INC(x) ((void)0)
#define NV2A_VK_DGROUP_END() ((void)0)
#define VK_NULL_HANDLE 0
#define REG_WORDS (0x10000 / 4)
#define NV2A_PROF_PIPELINE_NOTDIRTY 0

static unsigned ctz32(uint32_t value) { return __builtin_ctz(value); }
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "primitive-cache-types.inc"

typedef struct GPUProperties {
    struct { short tri; } geom_shader_winding;
} GPUProperties;
typedef struct VshState {
    unsigned uniform_attrs, swizzle_attrs, int16_attrs, compressed_attrs;
    unsigned surface_scale_factor;
    struct { bool texture_matrix_enable[4]; } fixed_function;
} VshState;
typedef struct ShaderState {
    VshState vsh;
    GeomState geom;
    struct { unsigned surface_zeta_format; } psh;
} ShaderState;
typedef struct ShaderBinding { ShaderState state; } ShaderBinding;
typedef struct VkVertexInputAttributeDescription {
    uint32_t location, binding, format, offset;
} VkVertexInputAttributeDescription;
typedef struct VkVertexInputBindingDescription {
    uint32_t binding, stride, inputRate;
} VkVertexInputBindingDescription;
typedef struct PipelineBinding {
    uintptr_t pipeline;
    struct {
        bool clear;
        VkVertexInputAttributeDescription attribute_descriptions[16];
        VkVertexInputBindingDescription binding_descriptions[16];
    } key;
} PipelineBinding;
typedef struct Backend {
    struct { GPUProperties *(*get_gpu_properties)(void); } ops;
} Backend;
typedef struct PGRAPHVkState {
    PipelineBinding *pipeline_binding;
    ShaderBinding *shader_binding;
    bool pipeline_binding_changed, shader_bindings_changed;
    bool pipeline_state_dirty, need_descriptor_rebind, uniforms_changed;
    bool texture_bindings_changed;
    bool framebuffer_dirty, in_command_buffer, in_render_pass;
    bool bindless_textures_supported, push_descriptors_supported;
    int framebuffer_index, ubo_descriptor_set_index;
    int push_ubo_set_index, descriptor_set_index;
    unsigned texture_vram_gen, last_texture_vram_gen;
    unsigned last_texture_state_gen, last_shader_state_gen;
    unsigned last_pipeline_state_gen, last_non_dynamic_reg_gen;
    unsigned pipeline_vertex_attr_gen;
    unsigned pipeline_num_active_attr_descs, pipeline_num_active_bind_descs;
    unsigned num_active_vertex_attribute_descriptions;
    unsigned num_active_vertex_binding_descriptions;
    VkVertexInputAttributeDescription vertex_attribute_descriptions[16];
    VkVertexInputBindingDescription vertex_binding_descriptions[16];
    uint64_t vertex_attribute_offsets[16];
} PGRAPHVkState;
typedef struct PGRAPHState {
    PGRAPHVkState *vk_renderer_state;
    Backend *renderer;
    uint32_t regs[REG_WORDS];
    bool dirty[REG_WORDS], render_pass_dirty;
    unsigned primitive_mode, uniform_attrs, swizzle_attrs;
    unsigned int16_attrs, compressed_attrs, surface_scale_factor;
    unsigned texture_state_gen, shader_state_gen, pipeline_state_gen;
    unsigned non_dynamic_reg_gen;
    unsigned vertex_attr_gen;
    struct { unsigned zeta_format; } surface_shape;
    bool texture_matrix_enable[4], program_data_dirty, clearing;
    bool vsh_constants_any_dirty, ltctxa_any_dirty;
    bool ltctxb_any_dirty, ltc1_any_dirty;
} PGRAPHState;

static uint32_t pgraph_reg_r(PGRAPHState *pg, unsigned reg)
{
    assert(reg % 4 == 0 && reg / 4 < REG_WORDS);
    return pg->regs[reg / 4];
}

static bool pgraph_is_reg_dirty(PGRAPHState *pg, unsigned reg)
{
    assert(reg % 4 == 0 && reg / 4 < REG_WORDS);
    return pg->dirty[reg / 4];
}

static bool check_render_pass_dirty(PGRAPHState *pg)
{
    return pg->render_pass_dirty;
}

#ifndef NEGATIVE_CONTROL
static unsigned uniform_updates, pipeline_fallbacks;
static float live_uniform, allocation_uniform;

static void nv2a_profile_inc_counter(unsigned counter)
{
    assert(counter == NV2A_PROF_PIPELINE_NOTDIRTY);
}

static void pgraph_vk_update_shader_uniforms(PGRAPHState *state)
{
    assert(state->vk_renderer_state->shader_binding);
    uniform_updates++;
    allocation_uniform = live_uniform;
}
#endif

#include "primitive-cache-functions.inc"

static PGRAPHState pg;
static PGRAPHVkState r;
static PipelineBinding pipeline;
static ShaderBinding shader;
static Backend backend;
static bool (*const guards[])(PGRAPHState *) = {
    guard_0, guard_1, guard_2, guard_3, guard_4, guard_5,
};

static void polygon(unsigned mode)
{
    SET_MASK(pg.regs[NV_PGRAPH_SETUPRASTER / 4],
             NV_PGRAPH_SETUPRASTER_FRONTFACEMODE, mode);
    SET_MASK(pg.regs[NV_PGRAPH_SETUPRASTER / 4],
             NV_PGRAPH_SETUPRASTER_BACKFACEMODE, mode);
}

static void reset(unsigned mode, unsigned poly)
{
    memset(&pg, 0, sizeof(pg));
    memset(&r, 0, sizeof(r));
    memset(&shader, 0, sizeof(shader));
    memset(&pipeline, 0, sizeof(pipeline));
    pg.vk_renderer_state = &r;
    pg.renderer = &backend;
    pg.primitive_mode = mode;
    pg.surface_scale_factor = shader.state.vsh.surface_scale_factor = 1;
    polygon(poly);
    pgraph_glsl_set_geom_state(&pg, &shader.state.geom);
    pipeline.pipeline = 1;
    r.pipeline_binding = &pipeline;
    r.shader_binding = &shader;
    r.in_command_buffer = r.in_render_pass = true;
    r.framebuffer_index = r.ubo_descriptor_set_index = 1;
    r.push_ubo_set_index = r.descriptor_set_index = 1;
    r.push_descriptors_supported = r.bindless_textures_supported = true;
}

static bool cache_guard(unsigned i)
{
    unsigned saved = pg.texture_state_gen;
    if (i == 4) {
        /* The bindless texture path is considered when textures changed. */
        pg.texture_state_gen++;
    }
    bool accepted = guards[i](&pg);
    pg.texture_state_gen = saved;
    return accepted;
}

static void mode_matrix(void)
{
    unsigned false_misses = 0;
    for (unsigned raw = PRIM_TYPE_POINTS; raw <= PRIM_TYPE_POLYGON; raw++) {
        for (unsigned poly = POLY_MODE_FILL; poly <= POLY_MODE_LINE; poly++) {
            reset(raw, poly);
            assert(pgraph_glsl_primitive_mode_matches(&pg, &shader.state.geom));
            assert(pg.primitive_mode == raw);
            for (unsigned i = 0; i < ARRAY_SIZE(guards); i++) {
#ifdef NEGATIVE_CONTROL
                bool expected =
                    raw == (unsigned)shader.state.geom.primitive_mode;
                assert(cache_guard(i) == expected);
                false_misses += !expected;
#else
                assert(cache_guard(i));
#endif
            }
#ifdef NEGATIVE_CONTROL
            assert(pgraph_glsl_check_shader_state_dirty(&pg, &shader.state) ==
                   (raw != (unsigned)shader.state.geom.primitive_mode));
#else
            assert(!pgraph_glsl_check_shader_state_dirty(&pg, &shader.state));
#endif
            GeomState cached = shader.state.geom;
            for (unsigned next = PRIM_TYPE_POINTS; next <= PRIM_TYPE_POLYGON;
                 next++) {
                for (unsigned next_poly = POLY_MODE_FILL;
                     next_poly <= POLY_MODE_LINE; next_poly++) {
                    pg.primitive_mode = next;
                    polygon(next_poly);
                    GeomState fresh = {0};
                    pgraph_glsl_set_geom_state(&pg, &fresh);
                    assert(pgraph_glsl_primitive_mode_matches(&pg, &cached) ==
                           (fresh.primitive_mode == cached.primitive_mode));
                    assert(pg.primitive_mode == next);
                }
            }
        }
    }
#ifdef NEGATIVE_CONTROL
    assert(false_misses == 21 * ARRAY_SIZE(guards));
    printf("negative control: original six guards reject 126/180 valid "
           "geometry matches (21 rewritten modes per guard)\n");
#else
    (void)false_misses;
    puts("primitive cache: 30 builder modes, 900 live-mode transitions, "
         "180 actual draw-guard matches passed");
#endif
}

#ifndef NEGATIVE_CONTROL
static void guard_fallbacks(void)
{
    for (unsigned poly = POLY_MODE_FILL; poly <= POLY_MODE_LINE; poly++) {
        reset(PRIM_TYPE_QUADS, poly);
        pg.primitive_mode = shader.state.geom.primitive_mode == PRIM_TYPE_POINTS
                            ? PRIM_TYPE_LINES : PRIM_TYPE_POINTS;
        assert(pgraph_glsl_check_shader_state_dirty(&pg, &shader.state));
        for (unsigned i = 0; i < ARRAY_SIZE(guards); i++) {
            assert(!cache_guard(i));
        }
        reset(PRIM_TYPE_QUADS, poly);
        polygon((poly + 1) % 3);
        pg.dirty[NV_PGRAPH_SETUPRASTER / 4] = true;
        pg.shader_state_gen++;
        pg.pipeline_state_gen++;
        pg.non_dynamic_reg_gen++;
        assert(pgraph_glsl_check_shader_state_dirty(&pg, &shader.state));
        for (unsigned i = 0; i < ARRAY_SIZE(guards); i++) {
            assert(!cache_guard(i));
        }
    }
    reset(PRIM_TYPE_TRIANGLE_STRIP, POLY_MODE_FILL);
    pg.program_data_dirty = true;
    assert(pgraph_glsl_check_shader_state_dirty(&pg, &shader.state));
    assert(!cache_guard(1) && !cache_guard(2) && !cache_guard(3));
    assert(!cache_guard(4) && !cache_guard(5));
    reset(PRIM_TYPE_TRIANGLE_STRIP, POLY_MODE_FILL);
    pg.vsh_constants_any_dirty = true;
    assert(!cache_guard(3) && !cache_guard(4));
    reset(PRIM_TYPE_TRIANGLE_STRIP, POLY_MODE_FILL);
    r.uniforms_changed = true;
    assert(!cache_guard(3) && !cache_guard(4));
    reset(PRIM_TYPE_TRIANGLE_STRIP, POLY_MODE_FILL);
    r.ubo_descriptor_set_index = r.push_ubo_set_index = 0;
    r.descriptor_set_index = 0;
    assert(!cache_guard(3) && !cache_guard(5));
    reset(PRIM_TYPE_TRIANGLE_STRIP, POLY_MODE_FILL);
    r.need_descriptor_rebind = true;
    assert(!cache_guard(3) && !cache_guard(4) && !cache_guard(5));
    reset(PRIM_TYPE_TRIANGLE_STRIP, POLY_MODE_FILL);
    r.framebuffer_dirty = true;
    assert(!cache_guard(3) && !cache_guard(4) && !cache_guard(5));
    reset(PRIM_TYPE_TRIANGLE_STRIP, POLY_MODE_FILL);
    pg.non_dynamic_reg_gen++;
    assert(!cache_guard(3) && !cache_guard(4));
    puts("primitive cache: topology/polygon, dirty program/generations, "
         "uniform and descriptor fallback checks passed");
}

static void pipeline_uniform_refresh(void)
{
    for (unsigned dirty = 0; dirty < 5; dirty++) {
        reset(PRIM_TYPE_TRIANGLE_STRIP, POLY_MODE_FILL);
        /* Force the full path: MFP is unavailable without descriptor state. */
        r.need_descriptor_rebind = true;
        r.ubo_descriptor_set_index = r.push_ubo_set_index = 0;
        r.descriptor_set_index = 0;
        assert(!cache_guard(5));
        if (dirty == 0) {
            r.uniforms_changed = true;
        } else if (dirty == 1) {
            pg.vsh_constants_any_dirty = true;
        } else if (dirty == 2) {
            pg.ltctxa_any_dirty = true;
        } else if (dirty == 3) {
            pg.ltctxb_any_dirty = true;
        } else {
            pg.ltc1_any_dirty = true;
        }
        live_uniform = 42;
        allocation_uniform = 17;
        uniform_updates = pipeline_fallbacks = 0;
        pipeline_fast_hit(&pg);
        assert(uniform_updates == 1 && pipeline_fallbacks == 0);
        assert(allocation_uniform == live_uniform);
    }
    reset(PRIM_TYPE_TRIANGLE_STRIP, POLY_MODE_FILL);
    uniform_updates = pipeline_fallbacks = 0;
    pipeline_fast_hit(&pg);
    assert(uniform_updates == 0 && pipeline_fallbacks == 0);
    pg.shader_state_gen++;
    pipeline_fast_hit(&pg);
    assert(uniform_updates == 0 && pipeline_fallbacks == 1);
    puts("primitive cache: actual early pipeline hit refreshes dirty "
         "uniforms with MFP unavailable; clean state skips refresh");
}

static void pipeline_layout_and_clear(void)
{
    reset(PRIM_TYPE_TRIANGLE_STRIP, POLY_MODE_FILL);
    /* create_clear_pipeline tags this key; end_draw ends its render pass. */
    pipeline.key.clear = true;
    r.in_render_pass = false;
    uniform_updates = pipeline_fallbacks = 0;
    assert(!cache_guard(0));
    assert(!cache_guard(3) && !cache_guard(4) && !cache_guard(5));
    pipeline_fast_hit(&pg);
    assert(pipeline_fallbacks == 1 && uniform_updates == 0);
    assert(check_pipeline_dirty(&pg));
    /* Same shader generation, different binder-derived attribute masks. */
    for (unsigned mask = 0; mask < 4; mask++) {
        reset(PRIM_TYPE_TRIANGLE_STRIP, POLY_MODE_FILL);
        unsigned *fields[] = { &pg.uniform_attrs, &pg.swizzle_attrs,
                               &pg.int16_attrs, &pg.compressed_attrs };
        *fields[mask] = 1;
        pg.vertex_attr_gen++;
        assert(pg.shader_state_gen == r.last_shader_state_gen);
        assert(pgraph_glsl_check_shader_state_dirty(&pg, &shader.state));
        for (unsigned i = 0; i < ARRAY_SIZE(guards); i++) {
            assert(!cache_guard(i));
        }
    }
    reset(PRIM_TYPE_TRIANGLE_STRIP, POLY_MODE_FILL);
    pg.vertex_attr_gen++;
    assert(!cache_guard(0));
    r.num_active_vertex_attribute_descriptions = 2;
    r.num_active_vertex_binding_descriptions = 2;
    r.pipeline_num_active_attr_descs = r.pipeline_num_active_bind_descs = 2;
    assert(!check_pipeline_dirty(&pg));
    /* Equal surviving prefix cannot hide a removed Vulkan attribute. */
    r.num_active_vertex_attribute_descriptions = 1;
    assert(check_pipeline_dirty(&pg));
    r.num_active_vertex_attribute_descriptions = 2;
    r.num_active_vertex_binding_descriptions = 1;
    assert(check_pipeline_dirty(&pg));
    r.num_active_vertex_binding_descriptions = 2;
    r.vertex_attribute_descriptions[0].format = 7;
    assert(check_pipeline_dirty(&pg));
    puts("primitive cache: clear-to-normal, four shader masks, layout "
         "generation and shrinking descriptor counts retain fallbacks");
}

static void same_generation_input_changes(void)
{
    reset(PRIM_TYPE_TRIANGLE_STRIP, POLY_MODE_FILL);
    r.num_active_vertex_attribute_descriptions = 1;
    r.num_active_vertex_binding_descriptions = 1;
    r.pipeline_num_active_attr_descs = r.pipeline_num_active_bind_descs = 1;
    r.vertex_attribute_descriptions[0].format = 1;
    r.vertex_binding_descriptions[0].stride = 32;
    pipeline.key.attribute_descriptions[0] = r.vertex_attribute_descriptions[0];
    pipeline.key.binding_descriptions[0] = r.vertex_binding_descriptions[0];
    const unsigned input_guards[] = { 0, 3, 4, 5 };
    assert(vertex_input_matches_pipeline(&r));
    for (unsigned test = 0; test < 3; test++) {
        if (test == 0) {
            /* RAM stride32 -> packed inline stride16, with the same gen. */
            r.vertex_binding_descriptions[0].stride = 16;
        } else if (test == 1) {
            r.vertex_binding_descriptions[0].stride = 32;
            r.vertex_attribute_descriptions[0].format = 2;
        } else {
            r.vertex_attribute_descriptions[0].format = 1;
            r.vertex_attribute_descriptions[0].offset = 4;
        }
        assert(pg.vertex_attr_gen == r.pipeline_vertex_attr_gen);
        assert(!vertex_input_matches_pipeline(&r));
        assert(check_pipeline_dirty(&pg));
        for (unsigned i = 0; i < ARRAY_SIZE(input_guards); i++) {
            assert(!cache_guard(input_guards[i]));
        }
    }
    r.vertex_attribute_descriptions[0].offset = 0;
    r.vertex_attribute_offsets[0] = 4096;
    assert(vertex_input_matches_pipeline(&r));
    assert(!check_pipeline_dirty(&pg));
    for (unsigned i = 0; i < ARRAY_SIZE(input_guards); i++) {
        assert(cache_guard(input_guards[i]));
    }
    puts("primitive cache: same-generation input stride/format/layout "
         "changes reject reuse; buffer-offset-only changes retain it");
}
#endif

int main(void)
{
    mode_matrix();
#ifndef NEGATIVE_CONTROL
    guard_fallbacks();
    pipeline_uniform_refresh();
    pipeline_layout_and_clear();
    same_generation_input_changes();
#endif
    printf("primitive fixture bindless compiled=%d\n", OPT_BINDLESS_TEXTURES);
    return 0;
}
