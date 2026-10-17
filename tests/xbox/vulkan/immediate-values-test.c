/*
 * Immediate setters refresh draw constants without a layout edit.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdbool.h>
#include <stdint.h>

#define VERTEX_IMMEDIATE_TEST
#define OPT_BINDLESS_TEXTURES 0
#define VK_SHADER_STAGE_VERTEX_BIT 1
#define NV2A_VERTEX_ATTR_DIFFUSE 3

typedef struct GeomState { unsigned primitive_mode; } GeomState;
typedef struct VshState {
    uint16_t uniform_attrs, compressed_attrs, swizzle_attrs, int16_attrs;
} VshState;
typedef struct ShaderBinding {
    struct {
        VshState vsh;
        GeomState geom;
    } state;
} ShaderBinding;
typedef struct PipelineBinding PipelineBinding;

#define main vertex_cache_test_main
#include "vertex-cache-test.c"
#undef main

struct PipelineBinding {
    unsigned layout;
    struct {
        bool clear;
        VkVertexInputAttributeDescription attribute_descriptions[16];
        VkVertexInputBindingDescription binding_descriptions[16];
    } key;
};

static float pushed[16][4], uniform_allocation[16][4];
static unsigned pushed_count, updates;
static ShaderBinding shader;
static PipelineBinding pipeline = { .layout = 1 };

static void pgraph_finish_inline_buffer_vertex(PGRAPHState *pg)
{
    (void)pg;
    assert(!"fixture does not emit position vertices");
}

static bool check_render_pass_dirty(PGRAPHState *pg)
{
    (void)pg;
    return false;
}

bool pgraph_glsl_primitive_mode_matches(PGRAPHState *pg, const GeomState *state)
{
    /* All cases keep a valid POINTS binding; mapping has a separate test. */
    assert(pg->primitive_mode == 1 && state->primitive_mode == 1);
    return true;
}

void pgraph_vk_update_shader_uniforms(PGRAPHState *pg)
{
    /* Observe the production dispatch, using the real constant-value getter. */
    updates++;
    memset(uniform_allocation, 0, sizeof(uniform_allocation));
    pgraph_get_inline_values(pg, pg->uniform_attrs, uniform_allocation, NULL);
}

static void vkCmdPushConstants(unsigned cmd, unsigned layout, unsigned stage,
                                unsigned offset, unsigned bytes,
                                const void *values)
{
    assert(cmd == 1 && layout == 1 && stage == VK_SHADER_STAGE_VERTEX_BIT);
    assert(!offset && bytes <= sizeof(pushed));
    pushed_count = bytes / sizeof(pushed[0]);
    memcpy(pushed, values, bytes);
}

#include "immediate-values-functions.inc"

typedef void (*Setter)(PGRAPHState *, unsigned, uint32_t);
static const struct {
    Setter setter;
    unsigned method;
    uint32_t first, second;
} setters[] = {
    { set_SET_VERTEX_DATA2F_M, NV097_SET_VERTEX_DATA2F_M + 3 * 8,
      0x3f000000, 0x3f800000 },
    { set_SET_VERTEX_DATA4F_M, NV097_SET_VERTEX_DATA4F_M + 3 * 16,
      0x3f000000, 0x3f800000 },
    { set_SET_VERTEX_DATA2S, NV097_SET_VERTEX_DATA2S + 3 * 4,
      0x00020001, 0x00040003 },
    { set_SET_VERTEX_DATA4UB, NV097_SET_VERTEX_DATA4UB + 3 * 4,
      0xff204060, 0xff608020 },
    { set_SET_VERTEX_DATA4S_M, NV097_SET_VERTEX_DATA4S_M + 3 * 8,
      0x00020001, 0x00040003 },
    { set_SET_DIFFUSE_COLOR4F, NV097_SET_DIFFUSE_COLOR4F,
      0x3f000000, 0x3f800000 },
};

static void bind_live(Environment *e, bool buffered)
{
    if (buffered) {
        pgraph_vk_bind_vertex_attributes_inline(&e->nv2a);
    } else {
        pgraph_vk_bind_vertex_attributes(&e->nv2a, 0, 1, false, 0, 0);
    }
    shader.state.vsh.uniform_attrs = e->nv2a.pgraph.uniform_attrs;
    shader.state.vsh.compressed_attrs = e->nv2a.pgraph.compressed_attrs;
    shader.state.vsh.swizzle_attrs = e->nv2a.pgraph.swizzle_attrs;
    shader.state.vsh.int16_attrs = e->nv2a.pgraph.int16_attrs;
    e->renderer.pipeline_vertex_attr_gen = e->nv2a.pgraph.vertex_attr_gen;
    e->renderer.pipeline_num_active_attr_descs =
        e->renderer.num_active_vertex_attribute_descriptions;
    e->renderer.pipeline_num_active_bind_descs =
        e->renderer.num_active_vertex_binding_descriptions;
    memcpy(pipeline.key.attribute_descriptions,
           e->renderer.vertex_attribute_descriptions,
           sizeof(pipeline.key.attribute_descriptions));
    memcpy(pipeline.key.binding_descriptions,
           e->renderer.vertex_binding_descriptions,
           sizeof(pipeline.key.binding_descriptions));
}

static void consume(Environment *e)
{
    PGRAPHState *pg = &e->nv2a.pgraph;
    memset(pushed, 0, sizeof(pushed));
    push_vertex_attr_values(pg);
    float expected[16][4] = {0};
    int count = 0;
    pgraph_get_inline_values(pg, pg->uniform_attrs, expected, &count);
    assert(pushed_count == (unsigned)count);
    assert(!memcmp(pushed, expected, sizeof(expected)));
    /* Descriptor consumption clears the sticky flag after updating values. */
    e->renderer.uniforms_changed = false;
}

static void prepare(Environment *e, bool buffered)
{
    reset(e, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F);
    PGRAPHState *pg = &e->nv2a.pgraph;
    pg->vertex_attributes[3].count = 0;
    pg->primitive_mode = shader.state.geom.primitive_mode = 1;
    e->renderer.pipeline_binding = &pipeline;
    e->renderer.shader_binding = &shader;
    e->renderer.command_buffer = 1;
    e->renderer.use_push_constants_for_uniform_attrs = true;
    if (buffered) {
        pg->inline_buffer_length = 1;
        pg->vertex_attributes[0].inline_buffer_populated = true;
        pg->vertex_attributes[0].inline_buffer[3] = 1;
    }
    bind_live(e, buffered);
    consume(e);
    updates = 0;
}

static void changed_setter(unsigned index, bool buffered)
{
    Environment e;
    prepare(&e, buffered);
    PGRAPHState *pg = &e.nv2a.pgraph;
    /* Setters run between draw batches, before inline history exists. */
    pg->inline_buffer_length = 0;
    setters[index].setter(pg, setters[index].method, setters[index].first);
    pg->inline_buffer_length = buffered ? 1 : 0;
    bind_live(&e, buffered);
    if (sfp_uniform_guard(&e.renderer)) {
        fputs("immediate setter change escaped the SFP uniform guard\n",
              stderr);
        abort();
    }
    assert(pg->vertex_attr_gen == 1);
    /* Refresh CPU UBO values before uploading descriptors on a cache hit. */
    run_pipeline_early(pg);
    assert(updates == 1);
    float expected[16][4] = {0};
    pgraph_get_inline_values(pg, pg->uniform_attrs, expected, NULL);
    assert(!memcmp(uniform_allocation, expected, sizeof(expected)));
    consume(&e);
    bind_live(&e, buffered);
    assert(sfp_uniform_guard(&e.renderer));
    run_pipeline_early(pg);
    assert(updates == 1);
    e.renderer.uniforms_changed = true;
    bind_live(&e, buffered);
    assert(e.renderer.uniforms_changed);
    consume(&e);
    pg->inline_buffer_length = 0;
    setters[index].setter(pg, setters[index].method, setters[index].second);
    pg->inline_buffer_length = buffered ? 1 : 0;
    bind_live(&e, buffered);
    assert(!sfp_uniform_guard(&e.renderer));
    consume(&e);
}

static void replay(bool reorder)
{
    Environment e;
    prepare(&e, false);
    PGRAPHState *pg = &e.nv2a.pgraph;
    pg->vertex_attributes[3].inline_value[0] = 7;
    bind_live(&e, false);
    consume(&e);
    float live[4];
    memcpy(live, pg->vertex_attributes[3].inline_value, sizeof(live));
    pg->vertex_attributes[3].inline_value[0] = 11;
    if (!reorder) {
        bind_live(&e, false);
    }
    consume(&e);
    if (reorder) {
        assert(e.renderer.immediate_attr_values[3][0] == 7);
    }
    memcpy(pg->vertex_attributes[3].inline_value, live, sizeof(live));
    if (reorder) {
        invalidate_reorder_values(&e.renderer);
    } else {
        invalidate_merged_values(&e.renderer);
    }
    assert(!e.renderer.immediate_attrs_valid && e.renderer.uniforms_changed);
    bind_live(&e, false);
    assert(!sfp_uniform_guard(&e.renderer));
    run_pipeline_early(pg);
    consume(&e);
}

static void ram_transitions(void)
{
    Environment e;
    prepare(&e, false);
    PGRAPHState *pg = &e.nv2a.pgraph;
    VertexAttribute *attr = &pg->vertex_attributes[3];
    attr->count = 4;
    attr->stride = 16;
    pg->vertex_attr_gen++;
    bind_live(&e, false);
    assert(!(e.renderer.immediate_attrs_valid & (1 << 3)));
    consume(&e);
    attr->count = 0;
    pg->vertex_attr_gen++;
    bind_live(&e, false);
    assert(e.renderer.uniforms_changed);
    consume(&e);
    bind_live(&e, false);
    assert(!e.renderer.uniforms_changed);
}

static void streamed_inline_values(void)
{
    Environment e;
    prepare(&e, true);
    e.nv2a.pgraph.vertex_attributes[0].inline_buffer[0] = 123;
    bind_live(&e, true);
    assert(!e.renderer.uniforms_changed);
    assert(!(e.renderer.immediate_attrs_valid & 1));
}

static void bitwise_values(void)
{
    Environment e;
    prepare(&e, false);
    const uint32_t values[] = {
        0x80000000, 0x00000000, 0x7fc00001, 0x7fc00001, 0x7fc00002,
    };
    for (unsigned i = 0; i < ARRAY_SIZE(values); i++) {
        set_SET_VERTEX_DATA4F_M(&e.nv2a.pgraph,
                               NV097_SET_VERTEX_DATA4F_M + 3 * 16 + 12,
                               values[i]);
        bind_live(&e, false);
        assert(e.renderer.uniforms_changed == (i != 3));
        consume(&e);
    }
}

static void buffered_uniform_with_array_count(void)
{
    Environment e;
    prepare(&e, true);
    PGRAPHState *pg = &e.nv2a.pgraph;
    pg->vertex_attributes[3].count = 4;
    pg->inline_buffer_length = 0;
    set_SET_VERTEX_DATA4F_M(pg, NV097_SET_VERTEX_DATA4F_M + 3 * 16,
                           0x3f800000);
    pg->inline_buffer_length = 1;
    assert(!pg->vertex_attributes[3].inline_buffer_populated);
    bind_live(&e, true);
    assert(e.renderer.uniforms_changed && (pg->uniform_attrs & (1 << 3)));
    run_pipeline_early(pg);
    consume(&e);
}

static void buffered_stream_with_zero_count(void)
{
    Environment e;
    prepare(&e, true);
    e.nv2a.pgraph.vertex_attributes[0].count = 0;
    e.nv2a.pgraph.vertex_attributes[0].inline_buffer[0] = 123;
    bind_live(&e, true);
    assert(!e.renderer.uniforms_changed);
    assert(!(e.renderer.immediate_attrs_valid & 1));
}

#ifdef HAVE_VERTEX_INPUT_MATCHES
static void same_generation_inline_layout(bool buffered)
{
    Environment e;
    prepare(&e, false);
    PGRAPHState *pg = &e.nv2a.pgraph;
    pg->vertex_attributes[0].stride = 32;
    pg->vertex_attr_gen++;
    bind_live(&e, false);
    consume(&e);
    unsigned generation = pg->vertex_attr_gen;
    assert(vertex_input_matches_pipeline(&e.renderer));
    if (buffered) {
        pg->inline_buffer_length = 1;
        pg->vertex_attributes[0].inline_buffer_populated = true;
        pgraph_vk_bind_vertex_attributes_inline(&e.nv2a);
    } else {
        pgraph_vk_bind_vertex_attributes(&e.nv2a, 0, 1, true, 16, 0);
    }
    assert(pg->vertex_attr_gen == generation);
    assert(e.renderer.vertex_binding_descriptions[0].stride == 16);
    assert(pipeline.key.binding_descriptions[0].stride == 32);
    assert(!vertex_input_matches_pipeline(&e.renderer));
}
#endif

int main(void)
{
    for (unsigned i = 0; i < ARRAY_SIZE(setters); i++) {
        changed_setter(i, false);
        changed_setter(i, true);
    }
    replay(false);
    replay(true);
    ram_transitions();
    streamed_inline_values();
    bitwise_values();
    buffered_uniform_with_array_count();
    buffered_stream_with_zero_count();
#ifdef HAVE_VERTEX_INPUT_MATCHES
    same_generation_inline_layout(false);
    same_generation_inline_layout(true);
#endif
    puts("immediate values: 21 setter/binder/consumer scenarios passed");
    return 0;
}
