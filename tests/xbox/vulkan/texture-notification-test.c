/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPT_ASYNC_COMPILE 1
#define OPT_DYNAMIC_STATES 0
#define OPT_DYNAMIC_BLEND 0
#define NV2A_MAX_TEXTURES 4
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define TARGET_PAGE_MASK (~(uint64_t)4095)
#define TARGET_PAGE_ALIGN(x) (((x) + 4095) & TARGET_PAGE_MASK)
#define DIRTY_MEMORY_NV2A_TEX 1
#define REG_CAT_TEXTURE 4
#define REG_CAT_SHADER 1
#define REG_CAT_PIPELINE 2
#define VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 1
#define VK_NULL_HANDLE 0
#define VK_PIPELINE_BIND_POINT_GRAPHICS 0
#define VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET 1
#define VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER 1
#define VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC 2
#define VK_SHADER_STAGE_FRAGMENT_BIT 1
#define VK_FINISH_REASON_NEED_BUFFER_SPACE 1
#define BUFFER_UNIFORM 0
#define BUFFER_UNIFORM_STAGING 0
#define VSH_UBO_BINDING 0
#define PSH_UBO_BINDING 1
#define PSH_TEX_BINDING 2
#define DESC_SET_CACHE_SIZE 8
#define OPT_STAT_INC(x) ((void)0)
#define VK_LOG_ERROR(...) abort()
#define qatomic_read(p) (*(p))
#define container_of(p, t, f) ((t *)((char *)(p) - offsetof(t, f)))
#define QTAILQ_FOREACH(v, h, f) \
    for ((v) = (h)->first; (v); (v) = (v)->next)
#define NV2A_VK_DGROUP_BEGIN(...) ((void)0)
#define NV2A_VK_DGROUP_END() ((void)0)
#define NV2A_VK_DPRINTF(...) ((void)0)
static unsigned ctz32(uint32_t x) { return __builtin_ctz(x); }
#include "hw/xbox/nv2a/nv2a_regs.h"
typedef uint64_t hwaddr;
typedef struct TextureBinding {
    struct TextureBinding *next;
    struct {
        uint64_t texture_vram_offset, texture_length;
        uint64_t palette_vram_offset, palette_length;
        struct { unsigned color_format; } state;
        float scale;
    } key;
    bool possibly_dirty, dirty_check_result;
    unsigned dirty_check_frame, submit_time;
    unsigned bindless_slot;
    bool bindless_descriptor_used;
    uint64_t seq, image_view, sampler;
} TextureBinding;
typedef uint64_t VkDeviceSize;
typedef uint64_t VkDescriptorSet;
typedef uint64_t VkDescriptorSetLayout;
typedef struct ImageInfo {
    unsigned imageLayout;
    uint64_t imageView, sampler;
} VkDescriptorImageInfo;
typedef struct VkDescriptorBufferInfo {
    uint64_t buffer, offset, range;
} VkDescriptorBufferInfo;
typedef struct VkWriteDescriptorSet {
    unsigned sType, dstBinding, dstArrayElement, descriptorType;
    unsigned descriptorCount;
    uint64_t dstSet;
    const VkDescriptorImageInfo *pImageInfo;
    const VkDescriptorBufferInfo *pBufferInfo;
} VkWriteDescriptorSet;
typedef struct ShaderUniformLayout {
    void *allocation;
    size_t total_size;
} ShaderUniformLayout;
typedef struct BasicColorFormatInfo { bool linear; } BasicColorFormatInfo;
static const BasicColorFormatInfo kelvin_color_format_info_map[] = {
    { false }, { true },
};
typedef struct PshUniformValues { float texScale[4]; } PshUniformValues;
struct DescSetCacheKey {
    uint64_t ubo_buffer, ubo_range[2];
    unsigned layouts[4];
    uint64_t views[4], samplers[4];
};
typedef struct DescSetCacheEntry {
    struct DescSetCacheKey key;
    unsigned epoch;
    int index;
} DescSetCacheEntry;
typedef struct ModuleInfo { ShaderUniformLayout uniforms; } ModuleInfo;
typedef struct ShaderBinding {
    struct { ModuleInfo *module_info; } vsh, psh;
    bool ready;
} ShaderBinding;
typedef struct PipelineBinding { uint64_t layout; } PipelineBinding;
typedef struct PGRAPHVkState {
    TextureBinding *texture_bindings[4], dummy_texture;
    struct { TextureBinding *first; } texture_active_list;
    bool texture_bindings_changed, pipeline_state_dirty;
    bool shader_bindings_changed, need_descriptor_rebind, push_tex_dirty;
    bool tex_surface_direct[4];
    uint64_t tex_surface_direct_views[4];
    unsigned tex_surface_direct_layout[4];
    VkDescriptorImageInfo push_tex_infos[4];
    unsigned texture_vram_gen, last_texture_vram_gen;
    unsigned last_texture_state_gen, submit_count;
    ShaderBinding *shader_binding;
    PipelineBinding *pipeline_binding;
    bool uniforms_changed, pre_draw_skipped, bindless_set_bound;
    bool push_descriptors_supported, bindless_textures_supported;
    bool color_drawn_in_cb, zeta_drawn_in_cb, queries_reset_in_cb;
    bool in_command_buffer;
    bool in_render_pass, framebuffer_dirty, pipeline_binding_changed;
    unsigned framebuffer_index;
    uint64_t current_framebuffer;
    uint64_t device, command_buffer, bindless_descriptor_set;
    unsigned tex_push_offset;
    struct { uint64_t buffer; } storage_buffers[1];
    VkDeviceSize uniform_buffer_offsets[2];
    struct {
        struct { unsigned minUniformBufferOffsetAlignment; } limits;
    } device_props;
    int descriptor_set_index, descriptor_set_count;
    int push_ubo_set_index, push_ubo_set_count;
    int ubo_descriptor_set_index, ubo_descriptor_set_count;
    VkDescriptorSet *descriptor_sets, *push_ubo_sets, *ubo_descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout, push_ubo_set_layout;
    VkDescriptorSetLayout ubo_set_layout;
    unsigned desc_set_cache_epoch;
    DescSetCacheEntry cache_guard;
    DescSetCacheEntry desc_set_cache[DESC_SET_CACHE_SIZE];
    struct {
        bool valid;
        uint32_t regs[8], shaderprog_bits;
    } tex_reg_cache[4];
} PGRAPHVkState;
_Static_assert(offsetof(PGRAPHVkState, desc_set_cache) -
                   offsetof(PGRAPHVkState, cache_guard) ==
                   sizeof(DescSetCacheEntry),
               "cache[-1] must overlap the sentinel for the old-source control");
typedef struct PGRAPHState {
    PGRAPHVkState *vk_renderer_state;
    uint32_t regs_[0x10000];
    uint64_t regs_dirty[0x10000 / 64];
    unsigned texture_state_gen, shader_state_gen, pipeline_state_gen;
    unsigned non_dynamic_reg_gen, any_reg_gen, frame_time;
    bool texture_dirty[4];
    bool clearing;
} PGRAPHState;
typedef struct MemoryRegion { bool dirty; } MemoryRegion;
typedef struct NV2AState {
    PGRAPHState pgraph;
    MemoryRegion *vram;
} NV2AState;
static uint8_t pgraph_reg_category_table[0x10000 / 4];
static uint32_t pgraph_reg_dynamic_mask_table[0x10000 / 4];
static TextureBinding texture_a, texture_b;
static unsigned actual_views[128][4], bound_views[4], written_sets;
static unsigned pushed_indices[4], attr_pushes;
static bool expect_current;
static bool recycle_on_lookup;
static float uniform_values[4], uploaded_values[4];
static uint64_t memory_region_size(MemoryRegion *m)
{
    (void)m;
    return 0x10000;
}
static bool memory_region_test_and_clear_dirty(MemoryRegion *m, hwaddr off,
                                               hwaddr size, unsigned client)
{
    assert(client == 1 && size && off < 0x10000);
    bool dirty = m->dirty && off == 0x1000;
    if (dirty) {
        m->dirty = false;
    }
    return dirty;
}
static void bitmap_set(uint64_t *map, unsigned start, unsigned count)
{
    assert(count == 1);
    map[start / 64] |= UINT64_C(1) << (start % 64);
}
static TextureBinding *lookup_cached_texture(uint32_t offset)
{
    assert(offset == 0x1000 || offset == 0x2000);
    if (recycle_on_lookup) {
        assert(offset == 0x2000);
        texture_a.key.scale = texture_b.key.scale;
        texture_a.key.state.color_format = texture_b.key.state.color_format;
        texture_a.image_view = texture_b.image_view;
        texture_a.seq++;
        return &texture_a;
    }
    return offset == 0x1000 ? &texture_a : &texture_b;
}
static void pgraph_vk_finish(PGRAPHState *pg, unsigned reason)
{
    (void)pg; (void)reason;
    abort();
}
static void pgraph_vk_flush_all_frames(PGRAPHState *pg)
{
    (void)pg;
    abort();
}
static void pgraph_vk_invalidate_desc_set_cache(PGRAPHVkState *r)
{
    (void)r;
    abort();
}
static bool pgraph_vk_buffer_has_space_for(PGRAPHState *pg, unsigned buffer,
                                          uint64_t size, unsigned align)
{
    (void)pg; (void)buffer; (void)size; (void)align;
    return true;
}
static uint64_t pgraph_vk_append_to_buffer(PGRAPHState *pg, unsigned buffer,
                                          void **data,
                                          const uint64_t *size, unsigned n,
                                          unsigned align)
{
    (void)pg; (void)buffer; (void)align;
    assert(n == 1);
    assert(*size == sizeof(uploaded_values) && *data);
    memcpy(uploaded_values, *data, *size);
    return 0;
}
static uint64_t fast_hash(const uint8_t *data, size_t n)
{
    uint64_t h = 0;
    for (size_t i = 0; i < n; i++) {
        h = h * 31 + data[i];
    }
    return h;
}
static void vkUpdateDescriptorSets(uint64_t device, unsigned n,
                                   const VkWriteDescriptorSet *writes,
                                   unsigned copies, const void *copy)
{
    (void)device; (void)copy;
    assert(!copies);
    for (unsigned i = 0; i < n; i++) {
        assert(writes[i].dstSet < 128);
        if (writes[i].pImageInfo) {
            unsigned unit = writes[i].dstBinding - PSH_TEX_BINDING;
            assert(unit < 4);
            actual_views[writes[i].dstSet][unit] =
                writes[i].pImageInfo->imageView;
            if (expect_current && unit == 0 &&
                writes[i].pImageInfo->imageView != 200) {
                fputs("plain cache OFF descriptor image invalid\n", stderr);
                exit(1);
            }
        }
    }
    written_sets++;
}
static void vkCmdPushDescriptorSetKHR(uint64_t cmd, unsigned point,
                                     uint64_t layout, unsigned set,
                                     unsigned n,
                                     const VkWriteDescriptorSet *writes)
{
    (void)cmd; (void)point; (void)layout;
    assert(set == 0 && n == 4);
    for (unsigned i = 0; i < n; i++) {
        bound_views[i] = writes[i].pImageInfo->imageView;
    }
}
static void vkCmdBindDescriptorSets(uint64_t cmd, unsigned point,
                                   uint64_t layout, unsigned first,
                                   unsigned n, const uint64_t *sets,
                                   unsigned offsets, const uint32_t *values)
{
    (void)cmd; (void)point; (void)layout; (void)values;
    assert(n == 1 && (*sets < 128));
    assert(offsets == 0 || offsets == 2);
    if (first == 0 && offsets) {
        memcpy(bound_views, actual_views[*sets], sizeof(bound_views));
    }
}
static void vkCmdPushConstants(uint64_t cmd, uint64_t layout,
                               unsigned stage, unsigned offset,
                               unsigned size, const uint32_t *values)
{
    (void)cmd; (void)layout; (void)stage; (void)offset;
    assert(size == sizeof(pushed_indices));
    memcpy(pushed_indices, values, size);
}
static void push_vertex_attr_values(PGRAPHState *pg)
{
    (void)pg;
    attr_pushes++;
}
#define pgraph_vk_reg_r pgraph_reg_r
#include "texture-notification-functions.inc"

static NV2AState d;
static PGRAPHVkState r;
static MemoryRegion memory;
static ModuleInfo module;
static ShaderBinding shader;
static PipelineBinding pipeline;
static uint64_t sets[16], push_sets[16], ubo_sets[16];

static void reset(unsigned mode)
{
    memset(&d, 0, sizeof(d));
    memset(&r, 0, sizeof(r));
    memset(actual_views, 0, sizeof(actual_views));
    memset(bound_views, 0, sizeof(bound_views));
    memset(pushed_indices, 0, sizeof(pushed_indices));
    memory.dirty = false;
    expect_current = false;
    recycle_on_lookup = false;
    for (unsigned i = 0; i < 4; i++) {
        uniform_values[i] = uploaded_values[i] = 1;
    }
    written_sets = attr_pushes = 0;
    d.pgraph.vk_renderer_state = &r;
    d.pgraph.frame_time = 9;
    d.vram = &memory;
    module.uniforms = (ShaderUniformLayout) {
        .allocation = uniform_values, .total_size = 16,
    };
    shader.vsh.module_info = shader.psh.module_info = &module;
    shader.ready = true;
    r.shader_binding = &shader;
    r.pipeline_binding = &pipeline;
    r.push_descriptors_supported = mode == 1;
    r.bindless_textures_supported = mode == 2;
    r.in_command_buffer = true;
    r.in_render_pass = true;
    r.framebuffer_index = r.current_framebuffer = 1;
    r.dummy_texture.key.scale = 1;
    r.dummy_texture.image_view = 1;
    r.device_props.limits.minUniformBufferOffsetAlignment = 16;
    r.desc_set_cache_epoch = 1;
    memset(&r.cache_guard, 0xa5, sizeof(r.cache_guard));
    r.descriptor_set_count = r.push_ubo_set_count = 16;
    r.ubo_descriptor_set_count = 16;
    r.descriptor_set_index = r.push_ubo_set_index = 1;
    r.ubo_descriptor_set_index = 1;
    r.descriptor_sets = sets;
    r.push_ubo_sets = push_sets;
    r.ubo_descriptor_sets = ubo_sets;
    for (unsigned i = 0; i < 16; i++) {
        sets[i] = i;
        push_sets[i] = 16 + i;
        ubo_sets[i] = 32 + i;
    }
    texture_a = (TextureBinding) {
        .key = { .texture_vram_offset = 0x1000, .texture_length = 0x1000,
                 .state.color_format = 1, .scale = 1 },
        .image_view = 100, .seq = 1, .next = &texture_b,
        .bindless_slot = 1,
    };
    texture_b = (TextureBinding) {
        .key = { .texture_vram_offset = 0x2000, .texture_length = 0x1000,
                 .state.color_format = 1, .scale = 1 },
        .image_view = 200, .seq = 2,
        .dirty_check_frame = d.pgraph.frame_time,
        .bindless_slot = 2,
    };
    r.texture_active_list.first = &texture_a;
    for (unsigned i = 0; i < 4; i++) {
        r.texture_bindings[i] = &r.dummy_texture;
        r.push_tex_infos[i].imageView = 100;
        actual_views[sets[0]][i] = 100;
    }
    r.texture_bindings[0] = &texture_a;
    d.pgraph.regs_[NV_PGRAPH_TEXCTL0_0] = NV_PGRAPH_TEXCTL0_0_ENABLE;
    d.pgraph.regs_[NV_PGRAPH_TEXOFFSET0] = 0x1000;
    pgraph_reg_category_table[NV_PGRAPH_TEXOFFSET0 / 4] = REG_CAT_TEXTURE;
}

static void select_b(bool dirty)
{
    memory.dirty = dirty;
    set_offset(&d.pgraph, NV097_SET_TEXTURE_OFFSET, 0x2000);
    assert(d.pgraph.texture_dirty[0] && d.pgraph.texture_state_gen == 1);
    assert(!d.pgraph.shader_state_gen && !d.pgraph.non_dynamic_reg_gen);
    poll_before_draw(&d);
    if (dirty) {
        assert(r.texture_bindings[0] == &texture_b);
        assert(r.texture_bindings_changed && r.pipeline_state_dirty);
    }
    pipeline_bind(&d.pgraph);
}

static void assert_current(unsigned mode)
{
    bool current = mode == 2 ? pushed_indices[0] == 2 : bound_views[0] == 200;
    if (!current) {
        fputs("texture notification lost before descriptor binding\n", stderr);
        exit(1);
    }
    assert(!r.texture_bindings_changed && !r.shader_bindings_changed);
}

static void double_bind(unsigned mode)
{
    reset(mode);
    select_b(true);
    pgraph_vk_update_descriptor_sets(&d.pgraph);
    consume_draw(&d.pgraph);
    assert_current(mode);
    unsigned writes = written_sets;
    for (unsigned i = 0; i < 50; i++) {
        pgraph_vk_bind_textures(&d);
        pgraph_vk_update_descriptor_sets(&d.pgraph);
        consume_draw(&d.pgraph);
        assert_current(mode);
    }
    assert(written_sets == writes);
}

static void lifetime_cases(unsigned mode)
{
    reset(mode);
    select_b(false);
    assert(r.texture_bindings_changed);
    /* Repeated setup, async skip and clears must retain unconsumed state. */
    for (unsigned i = 0; i < 3; i++) {
        pgraph_vk_bind_textures(&d);
    }
    shader.ready = false;
    pgraph_vk_update_descriptor_sets(&d.pgraph);
    assert(r.texture_bindings_changed);
    r.pre_draw_skipped = true;
    consume_draw(&d.pgraph);
    assert(r.texture_bindings_changed);
    r.pre_draw_skipped = false;
    d.pgraph.clearing = true;
    consume_draw(&d.pgraph);
    assert(r.texture_bindings_changed);
    d.pgraph.clearing = false;
    shader.ready = true;
    reset_command_state(&r);
    assert(r.texture_bindings_changed && r.need_descriptor_rebind);
    pgraph_vk_bind_textures(&d);
    pgraph_vk_update_descriptor_sets(&d.pgraph);
    consume_draw(&d.pgraph);
    assert_current(mode);
    assert(!r.need_descriptor_rebind);
}

static void cache_hit(void)
{
    reset(0);
    select_b(true);
    ShaderUniformLayout *layouts[2] = { &module.uniforms, &module.uniforms };
    struct DescSetCacheKey key;
    make_desc_set_cache_key(&r, layouts, &key);
    unsigned slot = fast_hash((const uint8_t *)&key, sizeof(key)) %
                    DESC_SET_CACHE_SIZE;
    r.desc_set_cache[slot].key = key;
    r.desc_set_cache[slot].epoch = r.desc_set_cache_epoch;
    r.desc_set_cache[slot].index = 0;
    r.descriptor_set_index = 2;
    actual_views[sets[0]][0] = 200;
    pgraph_vk_update_descriptor_sets(&d.pgraph);
    assert(!written_sets);
    assert(r.texture_bindings_changed);
    consume_draw(&d.pgraph);
    assert_current(0);
}

static void cache_off(void)
{
    reset(0);
    select_b(false);
    /* RC7 must enter the plain writer despite its independent flag loss. */
    r.shader_bindings_changed = true;
    expect_current = true;
    unsigned char before[sizeof(r.desc_set_cache)];
    DescSetCacheEntry sentinel = r.cache_guard;
    memcpy(before, r.desc_set_cache, sizeof(before));
    pgraph_vk_update_descriptor_sets(&d.pgraph);
    if (memcmp(&sentinel, &r.cache_guard, sizeof(sentinel))) {
        fputs("plain cache OFF descriptor sentinel corrupted\n", stderr);
        exit(1);
    }
    if (memcmp(before, r.desc_set_cache, sizeof(before))) {
        fputs("plain cache OFF descriptor cache was written\n", stderr);
        exit(1);
    }
    consume_draw(&d.pgraph);
    assert_current(0);
}

static void scale_cases(void)
{
    unsigned modes = OPT_BINDLESS_TEXTURES ? 3 : 2;
    for (unsigned mode = 0; mode < modes; mode++) {
        for (unsigned scenario = 0; scenario < 10; scenario++) {
            reset(mode);
            texture_a.key.scale = 3;
            bool changed = true;
            switch (scenario) {
            case 1:
                texture_a.key.scale = 1;
                changed = false;
                break;
            case 2:
            case 8:
                texture_a.key.state.color_format = 0;
                texture_b.key.state.color_format = 0;
                texture_b.key.scale = scenario == 8 ? 7 : 1;
                changed = false;
                break;
            case 3:
                r.texture_bindings[0] = &r.dummy_texture;
                texture_b.key.scale = 3;
                break;
            case 4:
                d.pgraph.regs_[NV_PGRAPH_TEXCTL0_0] = 0;
                break;
            case 5:
            case 6:
                recycle_on_lookup = true;
                if (scenario == 6) {
                    texture_b.key.scale = 3;
                    changed = false;
                }
                break;
            case 7:
                r.texture_bindings[0] = NULL;
                changed = false;
                break;
            case 9:
                texture_a.key.scale = 1;
                r.uniforms_changed = true;
                break;
            }
            set_offset(&d.pgraph, NV097_SET_TEXTURE_OFFSET, 0x2000);
            pipeline_bind(&d.pgraph);
            if (changed && texture_bind_preserves_draw_state(&r)) {
                fputs("texture scale change escaped fast-path guard\n", stderr);
                exit(1);
            }
            assert(r.uniforms_changed == changed);
            PshUniformValues values;
            get_texture_scales(&d.pgraph, &values);
            float expected = scenario == 3 || scenario == 6 ? 3 : 1;
            assert(values.texScale[0] == expected);
            /* The serializer boundary forwards the actual PSH getter bytes. */
            memcpy(uniform_values, values.texScale, sizeof(uniform_values));
            if (changed) {
                pgraph_vk_update_descriptor_sets(&d.pgraph);
                assert(uploaded_values[0] == expected);
                assert(!r.uniforms_changed);
            }
            if (scenario == 5 || scenario == 6) {
                assert(r.texture_bindings[0] == &texture_a);
                assert(texture_a.seq == 2);
                assert(r.texture_bindings_changed || mode == 1);
            }
            r.uniforms_changed = false;
            pgraph_vk_bind_textures(&d);
            assert(!r.uniforms_changed);
        }
    }
    printf("texScale: %u effective-scale cases; real PSH getter, fast-return "
           "guard and descriptor UBO upload passed (bindless compiled=%d)\n",
           modes * 10, OPT_BINDLESS_TEXTURES);
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "scale")) {
        scale_cases();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "cache-off")) {
        cache_off();
        puts("plain descriptor cache OFF: current images, no cache write");
        return 0;
    }
    unsigned modes = OPT_BINDLESS_TEXTURES ? 3 : 2;
    for (unsigned mode = 0; mode < modes; mode++) {
        double_bind(mode);
        lifetime_cases(mode);
    }
    cache_hit();
    printf("texture notifications: %u modes; real dirty poll, repeated binds, "
           "descriptor writes/pushes/cache hit, pending clear/async/setup, "
           "CB reset, and 50 clean repeats passed (bindless compiled=%d)\n",
           modes, OPT_BINDLESS_TEXTURES);
    return 0;
}
