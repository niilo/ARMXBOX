/*
 * Consume shader-change notifications after actual draw-state bindings.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdlib.h>

#define SHADER_CONSUMPTION_TEST
#define OPT_DYNAMIC_STATES 0
#define OPT_DYNAMIC_BLEND 0
#define VK_PIPELINE_BIND_POINT_GRAPHICS 0
#define NV2A_PROF_PIPELINE_BIND NV2A_PROF_PIPELINE_NOTDIRTY
#define main primitive_cache_test_main
#include "primitive-cache-test.c"
#undef main

static unsigned pipeline_binds, descriptor_binds, attr_pushes, texture_pushes;
static unsigned bindless_binds, pipeline_keys, descriptor_preparations;
static bool pending_at_bind;
static unsigned binding_order;

static bool pgraph_glsl_compare_shader_state(const ShaderState *a,
                                              const ShaderState *b)
{
    return memcmp(a, b, sizeof(*a)) != 0;
}

static ShaderBinding *get_shader_binding_for_state(PGRAPHVkState *state,
                                                   const ShaderState *next)
{
    (void)state;
    shader.state = *next;
    return &shader;
}

static void pgraph_vk_bind_shaders(PGRAPHState *state)
{
    /* The repeated-state fixture should never need a fresh shader lookup. */
    (void)state;
    assert(!"unexpected shader lookup");
}

static void begin_render_pass(PGRAPHState *state)
{
    state->vk_renderer_state->in_render_pass = true;
}

static void vkCmdBindPipeline(unsigned command, unsigned point,
                               uintptr_t handle)
{
    assert(command == 1 && point == VK_PIPELINE_BIND_POINT_GRAPHICS);
    assert(handle == pipeline.pipeline);
    assert(r.shader_bindings_changed == pending_at_bind);
    pipeline_binds++;
}

static void bind_descriptor_sets(PGRAPHState *state)
{
    assert(state->vk_renderer_state->shader_bindings_changed ==
           pending_at_bind);
    assert(binding_order == 0);
    binding_order = 1;
    descriptor_binds++;
}

static void push_vertex_attr_values(PGRAPHState *state)
{
    assert(state->vk_renderer_state->shader_bindings_changed ==
           pending_at_bind);
    assert(binding_order == 1);
    binding_order = 2;
    attr_pushes++;
}

#if OPT_BINDLESS_TEXTURES
static void bind_bindless_set(PGRAPHState *state)
{
    assert(state->vk_renderer_state->shader_bindings_changed ==
           pending_at_bind);
    assert(!binding_order);
    bindless_binds++;
}

static void push_texture_indices(PGRAPHState *state)
{
    assert(state->vk_renderer_state->shader_bindings_changed ==
           pending_at_bind);
    assert(binding_order == 2);
    binding_order = 3;
    texture_pushes++;
}
#endif

#include "shader-consumption-functions.inc"

static void reset_sequence(unsigned mode)
{
    reset(PRIM_TYPE_TRIANGLE_STRIP, POLY_MODE_FILL);
    r.push_descriptors_supported = mode == 1;
    r.bindless_textures_supported = mode == 2;
    r.command_buffer = 1;
    pipeline_binds = descriptor_binds = attr_pushes = texture_pushes = 0;
    bindless_binds = pipeline_keys = descriptor_preparations = 0;
    uniform_updates = pipeline_fallbacks = 0;
    pending_at_bind = false;
    binding_order = 0;
}

static void select_shader(unsigned uniform_mask)
{
    ShaderState next = shader.state;
    pg.uniform_attrs = next.vsh.uniform_attrs = uniform_mask;
    record_shader_change(&pg, &next);
    assert(r.shader_bindings_changed && r.uniforms_changed);
    assert(r.pipeline_state_dirty);
}

static void prepare_draw(void)
{
    if (guard_3(&pg)) {
        r.pre_draw_skipped = true;
        return;
    }
    r.pre_draw_skipped = false;
    unsigned previous_fallbacks = pipeline_fallbacks;
    pipeline_fast_hit(&pg);
    if (pipeline_fallbacks != previous_fallbacks) {
        prepare_shader_uniforms(&pg);
        if (check_pipeline_dirty(&pg)) {
            pipeline_keys++;
            r.pipeline_binding_changed = true;
        }
    }
    descriptor_preparations += need_descriptor_set(&r);
    /* The descriptor upload consumes uniforms and resets its rebind flag. */
    r.uniforms_changed = false;
    r.need_descriptor_rebind = false;
    r.ubo_descriptor_set_index = r.push_ubo_set_index = 1;
    r.descriptor_set_index = 1;
    r.in_command_buffer = true;
}

static void consume_draw(void)
{
    pending_at_bind = r.shader_bindings_changed;
    binding_order = 0;
    bind_pipeline_prefix(&pg);
    consume_shader_state(&pg);
}

static void identical_draws(unsigned mode)
{
    reset_sequence(mode);
    select_shader(1);
    prepare_draw();
    assert(uniform_updates == 1 && descriptor_preparations == 1);
    assert(pipeline_keys == 1);
    consume_draw();
    if (r.shader_bindings_changed) {
        fputs("consumed shader change still blocks repeated draws\n", stderr);
        abort();
    }
    assert(descriptor_binds == 1 && attr_pushes == 1);
    assert(pipeline_binds == 1);
    for (unsigned i = 0; i < 50; i++) {
        prepare_draw();
        assert(r.pre_draw_skipped);
        consume_draw();
    }
    assert(uniform_updates == 1 && descriptor_preparations == 1);
    assert(pipeline_keys == 1);
    assert(descriptor_binds == 1 && attr_pushes == 1);
    printf("fixed counts mode=%u draws=51 uniform-preps=%u pipeline-keys=%u "
           "descriptor-preps=%u\n", mode, uniform_updates, pipeline_keys,
           descriptor_preparations);
    select_shader(2);
    prepare_draw();
    consume_draw();
    assert(uniform_updates == 2 && descriptor_preparations == 2);
    assert(pipeline_keys == 2 && descriptor_binds == 2);
    assert(!r.shader_bindings_changed);
}

static void unconsumed_and_replay(unsigned mode)
{
    reset_sequence(mode);
    select_shader(1);
    prepare_draw();
    /* Snapshot/async setup has not recorded an ordinary draw's bindings. */
    assert(r.shader_bindings_changed && !descriptor_binds);
    pg.clearing = true;
    consume_shader_state(&pg);
    assert(r.shader_bindings_changed && !descriptor_binds);
    pg.clearing = false;
    r.pre_draw_skipped = true;
    consume_shader_state(&pg);
    assert(r.shader_bindings_changed && !descriptor_binds);
    r.pre_draw_skipped = false;
    consume_draw();
    assert(!r.shader_bindings_changed);
    restore_shader_notification(&r, true);
    assert(r.shader_bindings_changed);
    prepare_draw();
    consume_draw();
    assert(!r.shader_bindings_changed);
    restore_shader_notification(&r, false);
    assert(!r.shader_bindings_changed);
}

static void new_command_buffer(unsigned mode)
{
    reset_sequence(mode);
    select_shader(1);
    prepare_draw();
    consume_draw();
    reset_command_state(&r);
    r.in_render_pass = false;
    r.ubo_descriptor_set_index = r.push_ubo_set_index = 0;
    r.descriptor_set_index = 0;
    r.bindless_set_bound = false;
    assert(!guard_3(&pg));
    prepare_draw();
    assert(!r.pre_draw_skipped);
    consume_draw();
    assert(uniform_updates == 2 && descriptor_preparations == 2);
    assert(pipeline_binds == 2 && descriptor_binds == 2);
    assert(!r.need_descriptor_rebind && !r.shader_bindings_changed);
    assert(guard_3(&pg));
}

static void baseline_counts(unsigned mode)
{
    reset_sequence(mode);
    select_shader(1);
    for (unsigned i = 0; i < 51; i++) {
        prepare_draw();
        assert(!r.pre_draw_skipped);
        consume_draw();
        assert(r.shader_bindings_changed);
    }
    assert(uniform_updates == 51 && pipeline_keys == 51);
    assert(descriptor_preparations == 51);
    printf("baseline counts mode=%u draws=51 uniform-preps=%u "
           "pipeline-keys=%u descriptor-preps=%u\n", mode, uniform_updates,
           pipeline_keys, descriptor_preparations);
}

int main(int argc, char **argv)
{
    unsigned modes = OPT_BINDLESS_TEXTURES ? 3 : 2;
    if (argc > 1) {
        assert(!strcmp(argv[1], "baseline-counts"));
        for (unsigned mode = 0; mode < modes; mode++) {
            baseline_counts(mode);
        }
        return 0;
    }
    for (unsigned mode = 0; mode < modes; mode++) {
        identical_draws(mode);
        unconsumed_and_replay(mode);
        new_command_buffer(mode);
    }
    printf("shader consumption: %u mode/scenario combinations passed "
           "(bindless compiled=%d)\n", modes * 3, OPT_BINDLESS_TEXTURES);
    return 0;
}
