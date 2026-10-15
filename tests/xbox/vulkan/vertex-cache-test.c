/*
 * Compare cached vertex binding against the cold layout path using real code.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#ifndef MEMORY_SIZE
#define MEMORY_SIZE 8192
#endif
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define OPT_STAT_INC(x) ((void)0)
#define NV2A_VK_DGROUP_BEGIN(...) ((void)0)
#define NV2A_VK_DGROUP_END() ((void)0)
#define NV2A_VK_DPRINTF(...) ((void)0)
#define HWADDR_PRIx "llx"
#define NV2A_PROF_ATTR_BIND 0
#define VK_VERTEX_INPUT_RATE_VERTEX 0

enum {
    NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D,
    NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL,
    NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1,
    NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F,
    NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K,
    NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP,
};
typedef uint64_t hwaddr;
typedef unsigned VkFormat;
typedef struct VkVertexInputAttributeDescription {
    unsigned location, binding;
    VkFormat format;
    unsigned offset;
} VkVertexInputAttributeDescription;
typedef struct VkVertexInputBindingDescription {
    unsigned binding, stride, inputRate;
} VkVertexInputBindingDescription;
typedef struct VertexAttribute {
    unsigned format, count, size, stride, offset, inline_array_offset;
    bool dma_select;
    bool inline_buffer_populated;
    float inline_value[4];
    float inline_buffer[16];
} VertexAttribute;
typedef struct MemorySyncRequirement {
    hwaddr addr, size;
} MemorySyncRequirement;
typedef struct PGRAPHVkState {
#ifdef VERTEX_SYNC_TEST
    ram_addr_t vram_ram_addr;
    int current_frame, num_active_frames;
    size_t bitmap_size;
    unsigned long *cb_vertex_pages;
    FrameStagingState frame_staging[1];
#endif
#ifdef VERTEX_IMMEDIATE_TEST
    PipelineBinding *pipeline_binding;
    ShaderBinding *shader_binding;
    bool use_push_constants_for_uniform_attrs;
    bool shader_bindings_changed, pipeline_state_dirty;
    unsigned command_buffer;
    unsigned last_texture_state_gen, texture_vram_gen, last_texture_vram_gen;
    unsigned last_shader_state_gen, last_pipeline_state_gen;
    unsigned pipeline_vertex_attr_gen;
    unsigned pipeline_num_active_attr_descs, pipeline_num_active_bind_descs;
#endif
    uint16_t immediate_attrs_valid;
    float immediate_attr_values[NV2A_VERTEXSHADER_ATTRIBUTES][4];
    unsigned last_vertex_attr_gen;
    unsigned cached_num_active_bindings, cached_num_active_attrs;
    unsigned num_active_vertex_binding_descriptions;
    unsigned num_active_vertex_attribute_descriptions;
    uint16_t cached_compressed_attrs, cached_uniform_attrs;
    uint16_t cached_swizzle_attrs, cached_int16_attrs;
    bool sscaled_vertex_supported, uniforms_changed;
    VkVertexInputAttributeDescription vertex_attribute_descriptions[16];
    VkVertexInputAttributeDescription cached_attr_descs[16];
    VkVertexInputBindingDescription vertex_binding_descriptions[16];
    VkVertexInputBindingDescription cached_bind_descs[16];
    int vertex_attribute_to_description_location[16];
    int cached_attr_to_desc_loc[16];
    hwaddr vertex_attribute_offsets[16], cached_attr_offsets[16];
    struct { hwaddr base_addr; size_t stride; } cached_attr_layout[16];
    MemorySyncRequirement vertex_ram_buffer_syncs[16];
    size_t num_vertex_ram_buffer_syncs;
} PGRAPHVkState;
typedef struct PGRAPHState {
    PGRAPHVkState *vk_renderer_state;
    VertexAttribute vertex_attributes[16];
    uint16_t compressed_attrs, uniform_attrs, swizzle_attrs, int16_attrs;
    unsigned vertex_attr_gen;
    unsigned inline_buffer_length;
#ifdef VERTEX_IMMEDIATE_TEST
    unsigned texture_state_gen, shader_state_gen, pipeline_state_gen;
    unsigned primitive_mode;
    bool vsh_constants_any_dirty, ltctxa_any_dirty;
    bool ltctxb_any_dirty, ltc1_any_dirty;
#endif
    hwaddr dma_vertex_a, dma_vertex_b;
    uint8_t inline_array[256];
} PGRAPHState;
typedef struct NV2AState { PGRAPHState pgraph; uint8_t *vram_ptr; } NV2AState;
typedef struct Environment {
    NV2AState nv2a;
    PGRAPHVkState renderer;
    uint8_t memory[MEMORY_SIZE];
} Environment;

static void *nv_dma_map(NV2AState *d, hwaddr dma, hwaddr *length)
{
    assert(dma < MEMORY_SIZE);
    *length = MEMORY_SIZE - dma;
    return d->vram_ptr + dma;
}

static void nv2a_profile_inc_counter(int counter) { (void)counter; }

#include "vertex-cache-functions.inc"

static void reset(Environment *e, unsigned format)
{
    memset(e, 0, sizeof(*e));
    e->nv2a.vram_ptr = e->memory;
    PGRAPHState *pg = &e->nv2a.pgraph;
    pg->vk_renderer_state = &e->renderer;
    pg->vertex_attr_gen = 1;
    pg->dma_vertex_b = MEMORY_SIZE / 2;
    e->renderer.sscaled_vertex_supported = true;
    for (unsigned i = 0; i < 16; i++) {
        pg->vertex_attributes[i].inline_value[3] = 1.0f;
    }
    pg->vertex_attributes[0] = (VertexAttribute) {
        .format = NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F,
        .count = 4, .size = 4, .stride = 16, .offset = 64,
    };
    unsigned count = 4, size = 4;
    if (format == NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP) {
        count = 1;
    } else if (format <= NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL) {
        size = 1;
    } else if (format == NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1 ||
               format == NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K) {
        size = 2;
    }
    pg->vertex_attributes[3] = (VertexAttribute) {
        .format = format, .count = count, .size = size,
        .stride = 0, .offset = 2048,
    };
    const float vertices[8] = { 1, 2, 3, 1, 4, 5, 6, 1 };
    memcpy(e->memory + 64, vertices, sizeof(vertices));
}

static void clone(Environment *dst, const Environment *src)
{
    *dst = *src;
    dst->nv2a.vram_ptr = dst->memory;
    dst->nv2a.pgraph.vk_renderer_state = &dst->renderer;
}

static void bind_range(Environment *e, unsigned min, unsigned max,
                       unsigned provoking, bool force_cold)
{
    e->renderer.uniforms_changed = false;
    e->renderer.num_vertex_ram_buffer_syncs = 0;
    if (force_cold) {
        e->renderer.cached_num_active_bindings = 0;
    }
    pgraph_vk_bind_vertex_attributes(&e->nv2a, min, max, false, 0, provoking);
}

static void bind(Environment *e, unsigned provoking, bool force_cold)
{
    bind_range(e, 0, 1, provoking, force_cold);
}

static void compare(const Environment *cached, const Environment *cold)
{
    const PGRAPHState *a = &cached->nv2a.pgraph, *b = &cold->nv2a.pgraph;
    for (unsigned i = 0; i < 16; i++) {
        if (memcmp(a->vertex_attributes[i].inline_value,
                   b->vertex_attributes[i].inline_value, sizeof(float) * 4)) {
            fputs("cached inline values diverge from cold path\n", stderr);
            abort();
        }
    }
    assert(a->uniform_attrs == b->uniform_attrs);
    assert(a->compressed_attrs == b->compressed_attrs);
    assert(a->swizzle_attrs == b->swizzle_attrs);
    assert(a->int16_attrs == b->int16_attrs);
    assert(cached->renderer.num_active_vertex_binding_descriptions ==
           cold->renderer.num_active_vertex_binding_descriptions);
    assert(!memcmp(cached->renderer.vertex_attribute_descriptions,
                   cold->renderer.vertex_attribute_descriptions,
                   sizeof(cached->renderer.vertex_attribute_descriptions)));
    assert(!memcmp(cached->renderer.vertex_binding_descriptions,
                   cold->renderer.vertex_binding_descriptions,
                   sizeof(cached->renderer.vertex_binding_descriptions)));
    float av[16][4] = {0}, bv[16][4] = {0};
    int ac = 0, bc = 0;
    pgraph_get_inline_values((PGRAPHState *)a, a->uniform_attrs, av, &ac);
    pgraph_get_inline_values((PGRAPHState *)b, b->uniform_attrs, bv, &bc);
    assert(ac == bc && !memcmp(av, bv, sizeof(av)));
}

static void write_uniform(Environment *e, bool second)
{
    const VertexAttribute *attr = &e->nv2a.pgraph.vertex_attributes[3];
    uint8_t *p = e->memory + attr->offset;
    if (attr->format == NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F) {
        const float values[4] = { second ? 7 : 1, 2, 3, 1 };
        memcpy(p, values, sizeof(values));
    } else if (attr->format == NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP) {
        uint32_t value = second ? 0x1ff00401 : 0x3ff003ff;
        memcpy(p, &value, sizeof(value));
    } else {
        const uint8_t values[8] = { second ? 127 : 1, 2, 3, 4, 5, 6, 7, 8 };
        memcpy(p, values, sizeof(values));
    }
}

static void uniform_mutation(unsigned format)
{
    Environment cached, cold;
    reset(&cached, format);
    write_uniform(&cached, false);
    bind(&cached, 0, false);
    assert(cached.renderer.cached_num_active_bindings > 0);
    clone(&cold, &cached);
    write_uniform(&cached, true);
    write_uniform(&cold, true);
    bind(&cached, 0, false);
    bind(&cold, 0, true);
    compare(&cached, &cold);
    assert(cached.renderer.uniforms_changed && cold.renderer.uniforms_changed);
    assert(cached.nv2a.pgraph.vertex_attr_gen == 1);
    /* Identical decoded values must retain the no-update path. */
    bind(&cached, 0, false);
    bind(&cold, 0, true);
    compare(&cached, &cold);
    assert(!cached.renderer.uniforms_changed);
    assert(!cold.renderer.uniforms_changed);
}

static void provoking_and_latch(bool mutate_memory)
{
    Environment cached, cold;
    reset(&cached, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F);
    write_uniform(&cached, false);
    bind(&cached, 0, false);
    clone(&cold, &cached);
    if (mutate_memory) {
        float replacement = 123.0f;
        memcpy(cached.memory + 80, &replacement, sizeof(replacement));
        memcpy(cold.memory + 80, &replacement, sizeof(replacement));
    }
    bind(&cached, 1, false);
    bind(&cold, 1, true);
    compare(&cached, &cold);
    assert(!cached.renderer.uniforms_changed);
    assert(cached.nv2a.pgraph.vertex_attributes[0].inline_value[0] ==
           (mutate_memory ? 123.0f : 4.0f));
    /* Disabling the array exposes the last provoking value as a uniform. */
    cached.nv2a.pgraph.vertex_attributes[0].count = 0;
    cold.nv2a.pgraph.vertex_attributes[0].count = 0;
    cached.nv2a.pgraph.vertex_attr_gen++;
    cold.nv2a.pgraph.vertex_attr_gen++;
    bind(&cached, 1, false);
    bind(&cold, 1, true);
    compare(&cached, &cold);
    assert(cached.nv2a.pgraph.uniform_attrs & 1);
}

static void generation_and_disabled(void)
{
    Environment cached, cold;
    reset(&cached, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F);
    write_uniform(&cached, false);
    bind(&cached, 0, false);
    clone(&cold, &cached);
    /* Method handlers bump the layout generation on offset/DMA changes. */
    cached.nv2a.pgraph.dma_vertex_a = cold.nv2a.pgraph.dma_vertex_a = 4096;
    cached.nv2a.pgraph.vertex_attr_gen++;
    cold.nv2a.pgraph.vertex_attr_gen++;
    const float value[4] = { 19, 20, 21, 1 };
    memcpy(cached.memory + 4096 + 2048, value, sizeof(value));
    memcpy(cold.memory + 4096 + 2048, value, sizeof(value));
    bind(&cached, 0, false);
    bind(&cold, 0, true);
    compare(&cached, &cold);
    assert(cached.renderer.cached_attr_layout[3].base_addr == 6144);
    assert(cached.nv2a.pgraph.vertex_attributes[3].inline_value[0] == 19);
    /* Disabled arrays retain explicit method values, rather than RAM at0. */
    cached.nv2a.pgraph.vertex_attributes[5].inline_value[0] = 55;
    cold.nv2a.pgraph.vertex_attributes[5].inline_value[0] = 55;
    bind(&cached, 0, false);
    bind(&cold, 0, true);
    compare(&cached, &cold);
    assert(cached.nv2a.pgraph.vertex_attributes[5].inline_value[0] == 55);
}

static void nonzero_minimum(void)
{
    Environment cached, cold;
    reset(&cached, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F);
    write_uniform(&cached, false);
    bind(&cached, 0, false);
    clone(&cold, &cached);
    const float vertices[8] = { 31, 32, 33, 1, 41, 42, 43, 1 };
    memcpy(cached.memory + 64 + 2 * 16, vertices, sizeof(vertices));
    memcpy(cold.memory + 64 + 2 * 16, vertices, sizeof(vertices));
    bind_range(&cached, 2, 3, 3, false);
    bind_range(&cold, 2, 3, 3, true);
    compare(&cached, &cold);
    assert(cached.nv2a.pgraph.vertex_attributes[0].inline_value[0] == 41);
    assert(!cached.renderer.uniforms_changed);
    assert(cached.renderer.vertex_ram_buffer_syncs[0].addr == 96);
    assert(cached.renderer.vertex_ram_buffer_syncs[0].size == 32);
}

static void format_and_offset_generation(bool change_format)
{
    Environment cached, cold;
    reset(&cached, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F);
    write_uniform(&cached, false);
    bind(&cached, 0, false);
    clone(&cold, &cached);
    VertexAttribute *a = &cached.nv2a.pgraph.vertex_attributes[3];
    VertexAttribute *b = &cold.nv2a.pgraph.vertex_attributes[3];
    if (change_format) {
        a->format = b->format = NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL;
        a->size = b->size = 1;
    } else {
        a->offset = b->offset = 2080;
    }
    cached.nv2a.pgraph.vertex_attr_gen++;
    cold.nv2a.pgraph.vertex_attr_gen++;
    write_uniform(&cached, true);
    write_uniform(&cold, true);
    bind(&cached, 0, false);
    bind(&cold, 0, true);
    compare(&cached, &cold);
    assert(cached.renderer.uniforms_changed);
    assert(cached.renderer.last_vertex_attr_gen == 2);
    assert(cached.renderer.cached_attr_layout[3].base_addr == a->offset);
    bind(&cached, 0, false);
    assert(!cached.renderer.uniforms_changed);
}

static void all_ram_constants(void)
{
    Environment cached, cold;
    reset(&cached, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F);
    cached.nv2a.pgraph.vertex_attributes[0].count = 0;
    write_uniform(&cached, false);
    bind(&cached, 0, false);
    assert(!cached.renderer.cached_num_active_bindings);
    clone(&cold, &cached);
    write_uniform(&cached, true);
    write_uniform(&cold, true);
    bind(&cached, 1, false);
    bind(&cold, 1, true);
    compare(&cached, &cold);
    assert(cached.renderer.uniforms_changed);
    assert(!cached.renderer.cached_num_active_bindings);
    bind(&cached, 1, false);
    assert(!cached.renderer.uniforms_changed);
}

int main(void)
{
    for (unsigned format = 0; format < 6; format++) {
        uniform_mutation(format);
    }
    provoking_and_latch(false);
    provoking_and_latch(true);
    generation_and_disabled();
    nonzero_minimum();
    format_and_offset_generation(false);
    format_and_offset_generation(true);
    all_ram_constants();
    puts("vertex binding cache: 13 differential scenarios passed");
    return 0;
}
