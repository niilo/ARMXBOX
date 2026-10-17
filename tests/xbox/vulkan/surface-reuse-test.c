/*
 * Direct surface cache reuse: real branches, helpers and descriptor inputs.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NV2A_MAX_TEXTURES 4
#define BUFFER_UNIFORM 0
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define container_of(p, type, field) \
    ((type *)((char *)(p) - offsetof(type, field)))
#define OPT_STAT_INC(x) ((void)0)
#define QTAILQ_FOREACH(v, h, field) \
    for ((v) = (h)->first; (v); (v) = (v)->active_next)
#define VK_NULL_HANDLE 0
enum {
    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    VK_IMAGE_ASPECT_COLOR_BIT = 1, VK_IMAGE_ASPECT_DEPTH_BIT = 2,
    VK_IMAGE_ASPECT_STENCIL_BIT = 4,
    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT = 1,
    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT = 2,
    VK_ACCESS_SHADER_READ_BIT = 4,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT = 1,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT = 2,
    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT = 4,
    VK_QUEUE_FAMILY_IGNORED = -1,
    VK_FINISH_REASON_SURFACE_DOWN,
    NV2A_PROF_SURF_TO_TEX,
};
typedef unsigned VkImageLayout;
typedef uintptr_t VkImageView;
typedef unsigned VkCommandBuffer;
typedef struct VkDescriptorImageInfo {
    uintptr_t sampler;
    VkImageView imageView;
    VkImageLayout imageLayout;
} VkDescriptorImageInfo;
typedef struct VkWriteDescriptorSet {
    unsigned sType;
    uintptr_t dstSet;
    unsigned dstBinding, dstArrayElement, descriptorType, descriptorCount;
    const VkDescriptorImageInfo *pImageInfo;
} VkWriteDescriptorSet;
typedef struct VkImageMemoryBarrier {
    unsigned sType;
    VkImageLayout oldLayout, newLayout;
    unsigned srcQueueFamilyIndex, dstQueueFamilyIndex;
    uintptr_t image;
    struct {
        unsigned aspectMask, baseMipLevel, levelCount;
        unsigned baseArrayLayer, layerCount;
    } subresourceRange;
    unsigned srcAccessMask, dstAccessMask;
} VkImageMemoryBarrier;
typedef struct SurfaceBinding {
    bool color;
    unsigned draw_time;
    uintptr_t image;
    VkImageView image_view;
    VkImageLayout image_layout;
    struct { unsigned aspect; } host_fmt;
} SurfaceBinding;
typedef struct TextureBinding {
    struct TextureBinding *active_next;
    unsigned draw_time, submit_time;
    uintptr_t sampler;
    VkImageView image_view;
    uint64_t hash;
    bool possibly_dirty;
#if OPT_BINDLESS_TEXTURES
    unsigned bindless_slot, bindless_binding;
    VkDescriptorImageInfo bindless_image_info;
    bool bindless_descriptor_used;
#endif
} TextureBinding;
typedef struct PGRAPHVkState {
    bool tex_surface_direct[4], texture_bindings_changed;
    VkImageView tex_surface_direct_views[4];
    VkImageLayout tex_surface_direct_layout[4];
    TextureBinding *texture_bindings[4];
    struct { TextureBinding *first; } texture_active_list;
    struct { unsigned count; } reorder_window, draw_queue;
    bool bindless_textures_supported;
    unsigned device, bindless_descriptor_set, submit_count, num_active_frames;
    struct { uintptr_t buffer; } storage_buffers[1];
    bool in_command_buffer, in_render_pass, framebuffer_dirty;
    bool need_descriptor_rebind, uniforms_changed, pipeline_binding_changed;
    bool shader_bindings_changed;
    unsigned framebuffer_index;
    uintptr_t current_framebuffer;
} PGRAPHVkState;
typedef struct PGRAPHState { PGRAPHVkState *vk_renderer_state; } PGRAPHState;
typedef struct NV2AState { PGRAPHState pgraph; } NV2AState;
typedef struct ShaderUniformLayout { size_t total_size; } ShaderUniformLayout;
struct DescSetCacheKey {
    uintptr_t ubo_buffer;
    size_t ubo_range[2];
    VkImageLayout layouts[4];
    VkImageView views[4];
    uintptr_t samplers[4];
};

static PGRAPHVkState renderer;
static NV2AState nv2a = { .pgraph = { &renderer } };
static TextureBinding texture;
static SurfaceBinding surface;
static bool direct_allowed, open_reader, submitted_reader;
static unsigned barriers, color_preparations, depth_preparations;
static unsigned finishes, flushes, descriptor_writes, copies, uploads;
static VkDescriptorImageInfo actual_descriptor, retired_descriptor;

static void pgraph_vk_flush_reorder_window(NV2AState *d) { (void)d; }
static void pgraph_vk_flush_draw_queue(NV2AState *d) { (void)d; }
static void nv2a_profile_inc_counter(unsigned counter) { (void)counter; }

static VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *pg)
{
    pg->vk_renderer_state->in_render_pass = false;
    return 1;
}

static void pgraph_vk_end_nondraw_commands(PGRAPHState *pg, unsigned cmd)
{
    (void)pg;
    assert(cmd == 1);
}

static void vkCmdPipelineBarrier(unsigned cmd, unsigned src, unsigned dst,
                                 unsigned flags, unsigned nm, const void *m,
                                 unsigned nb, const void *b, unsigned ni,
                                 const VkImageMemoryBarrier *image)
{
    (void)flags; (void)m; (void)b;
    assert(cmd == 1 && !nm && !nb && ni == 1);
    assert(dst == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    if (src == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) {
        assert(image->newLayout == VK_IMAGE_LAYOUT_GENERAL);
        color_preparations++;
    } else {
        assert(src == VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
        assert(image->newLayout ==
               VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        depth_preparations++;
    }
    barriers++;
}

static void pgraph_vk_finish(PGRAPHState *pg, int reason)
{
    (void)reason;
    /* Finish submits the open draw before the shared descriptor can change. */
    if (open_reader) {
        retired_descriptor = actual_descriptor;
        open_reader = false;
        submitted_reader = true;
    }
    PGRAPHVkState *r = pg->vk_renderer_state;
    r->in_command_buffer = r->in_render_pass = false;
    r->current_framebuffer = r->framebuffer_index = 0;
    r->need_descriptor_rebind = r->uniforms_changed = true;
    finishes++;
}

static void pgraph_vk_flush_all_frames(PGRAPHState *pg)
{
    (void)pg;
    submitted_reader = false;
    flushes++;
}

static void vkUpdateDescriptorSets(unsigned device, unsigned count,
                                   const VkWriteDescriptorSet *write,
                                   unsigned copy_count, const void *copy)
{
    (void)device; (void)copy;
    assert(count == 1 && !copy_count);
    assert(!open_reader && !submitted_reader);
    actual_descriptor = *write->pImageInfo;
    descriptor_writes++;
}

static bool can_bind_surface_direct(PGRAPHVkState *r, const SurfaceBinding *s)
{
    (void)r; (void)s;
    return direct_allowed;
}

static void copy_surface_to_texture(PGRAPHState *pg, SurfaceBinding *s,
                                    TextureBinding *t)
{
    (void)pg;
    t->draw_time = s->draw_time;
    copies++;
}

static void upload_texture_image(PGRAPHState *pg, int unit, TextureBinding *t)
{
    (void)pg; (void)unit; (void)t;
    uploads++;
}

#include "surface-reuse-functions.inc"

static void reset(bool depth, bool bindless)
{
    memset(&renderer, 0, sizeof(renderer));
    memset(&texture, 0, sizeof(texture));
    texture.image_view = 100;
    texture.sampler = 200;
    renderer.num_active_frames = 2;
    renderer.submit_count = 100;
    renderer.bindless_textures_supported = bindless;
    renderer.in_command_buffer = renderer.in_render_pass = true;
    renderer.current_framebuffer = renderer.framebuffer_index = 1;
    for (unsigned i = 0; i < NV2A_MAX_TEXTURES; i++) {
        renderer.texture_bindings[i] = &texture;
    }
    surface = (SurfaceBinding) {
        .color = !depth, .draw_time = 7, .image = 300, .image_view = 301,
        .image_layout = depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                              : VK_IMAGE_LAYOUT_GENERAL,
        .host_fmt.aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT
                                 : VK_IMAGE_ASPECT_COLOR_BIT,
    };
    actual_descriptor = (VkDescriptorImageInfo) {
        .imageView = texture.image_view, .sampler = texture.sampler,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
#if OPT_BINDLESS_TEXTURES
    texture.bindless_image_info = actual_descriptor;
    texture.bindless_slot = 1;
#endif
    direct_allowed = true;
    open_reader = submitted_reader = false;
    barriers = color_preparations = depth_preparations = 0;
    finishes = flushes = descriptor_writes = copies = uploads = 0;
}

static void cached(unsigned unit, bool source_is_surface)
{
    exercise_cached(&nv2a.pgraph, unit, &surface, &texture, source_is_surface);
}

static void check_unit(unsigned unit)
{
    ShaderUniformLayout u = { .total_size = 16 };
    ShaderUniformLayout *layouts[2] = { &u, &u };
    struct DescSetCacheKey key;
    make_desc_set_cache_key(&renderer, layouts, &key);
    VkImageLayout expected = surface.color ? VK_IMAGE_LAYOUT_GENERAL :
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    if (!renderer.tex_surface_direct[unit] ||
        key.views[unit] != surface.image_view ||
        key.layouts[unit] != expected ||
        !renderer.texture_bindings_changed) {
        fputs("cached surface descriptor inputs are stale\n", stderr);
        abort();
    }
#if OPT_BINDLESS_TEXTURES
    if (renderer.bindless_textures_supported) {
        assert(actual_descriptor.imageView == surface.image_view);
        assert(actual_descriptor.imageLayout == expected);
    }
#endif
}

static void readers(void)
{
    update_timestamps(&renderer);
    if (renderer.bindless_textures_supported) {
        open_reader = submitted_reader = true;
    }
}

static void shared_units(bool depth, bool bindless)
{
    reset(depth, bindless);
    cached(0, true);
    check_unit(0);
    readers();
    unsigned previous_writes = descriptor_writes;
    renderer.tex_surface_direct_layout[1] = VK_IMAGE_LAYOUT_UNDEFINED;
    renderer.texture_bindings_changed = false;
    cached(1, true);
    check_unit(1);
    assert(descriptor_writes == previous_writes && !finishes && !flushes);
    unsigned previous_barriers = barriers;
    renderer.texture_bindings_changed = false;
    cached(1, true);
    check_unit(1);
    assert(barriers == previous_barriers);
    assert(descriptor_writes == previous_writes && !finishes && !flushes);
}

static void zero_stamp(bool bindless)
{
    reset(false, bindless);
    surface.draw_time = texture.draw_time = 0;
    readers();
    cached(0, true);
    check_unit(0);
    assert(color_preparations == 1);
    if (renderer.bindless_textures_supported) {
        assert(finishes == 1 && flushes == 1 && descriptor_writes == 1);
        assert(retired_descriptor.imageView == texture.image_view);
    }
}

static void depth_attachment_rebind(bool bindless)
{
    reset(true, bindless);
    cached(0, true);
    readers();
    unsigned previous = barriers;
    surface.image_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    cached(0, true);
    check_unit(0);
    assert(surface.image_layout ==
           VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    assert(barriers == previous + 1);
    assert(!finishes && !flushes);
}

static void normal_transition(bool bindless)
{
    reset(false, bindless);
    cached(0, true);
    readers();
    unsigned previous_writes = descriptor_writes;
    cached(0, false);
    assert(!renderer.tex_surface_direct[0] && uploads == 1);
    if (renderer.bindless_textures_supported) {
        assert(actual_descriptor.imageView == texture.image_view);
        assert(actual_descriptor.imageLayout ==
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        assert(descriptor_writes == previous_writes + 1);
        assert(finishes == 1 && flushes >= 1);
        assert(retired_descriptor.imageView == surface.image_view);
    }
}

static void changed_view(bool bindless)
{
    reset(false, bindless);
    cached(0, true);
    readers();
    VkImageView previous_view = surface.image_view;
    surface.image_view++;
    cached(0, true);
    check_unit(0);
    assert(color_preparations == 2);
    if (renderer.bindless_textures_supported) {
        assert(finishes == 1 && flushes == 1);
        assert(retired_descriptor.imageView == previous_view);
    }
}

static void blocked_driver(void)
{
    reset(false, false);
    direct_allowed = false;
    cached(0, true);
    assert(copies == 1 && !renderer.tex_surface_direct[0]);
    assert(!barriers && !descriptor_writes);
}

static void fast_path_revalidation(void)
{
    reset(false, false);
    assert(texture_bind_preserves_draw_state(&renderer));
    bool *invalidating[] = {
        &renderer.framebuffer_dirty, &renderer.need_descriptor_rebind,
        &renderer.uniforms_changed, &renderer.pipeline_binding_changed,
        &renderer.shader_bindings_changed,
    };
    for (unsigned i = 0; i < ARRAY_SIZE(invalidating); i++) {
        *invalidating[i] = true;
        assert(!texture_bind_preserves_draw_state(&renderer));
        *invalidating[i] = false;
    }
    renderer.in_render_pass = false;
    assert(!texture_bind_preserves_draw_state(&renderer));
    renderer.in_render_pass = true;
    renderer.in_command_buffer = false;
    assert(!texture_bind_preserves_draw_state(&renderer));
    renderer.in_command_buffer = true;
    renderer.framebuffer_index = 0;
    assert(!texture_bind_preserves_draw_state(&renderer));
    renderer.framebuffer_index = 1;
    renderer.current_framebuffer = 0;
    assert(!texture_bind_preserves_draw_state(&renderer));
    renderer.current_framebuffer = 1;
    cached(0, true);
    assert(!texture_bind_preserves_draw_state(&renderer));
#if OPT_BINDLESS_TEXTURES
    reset(false, true);
    readers();
    surface.draw_time = texture.draw_time = 0;
    cached(0, true);
    assert(finishes == 1);
    assert(!texture_bind_preserves_draw_state(&renderer));
#endif
}

static void destroyed_view_reuse(void)
{
#if OPT_BINDLESS_TEXTURES
    reset(false, true);
    cached(0, true);
    readers();
    TextureBinding retained = texture;
    retained.bindless_slot = 2;
    texture.bindless_image_info.imageView = 999;
    texture.active_next = &retained;
    retained.active_next = NULL;
    renderer.texture_active_list.first = &texture;
    /* Retained is cached but unbound; old uses retired before destruction. */
    open_reader = submitted_reader = false;
    pgraph_vk_invalidate_surface_bindless_view(&renderer, surface.image_view);
    assert(!retained.bindless_image_info.imageView);
    assert(!retained.bindless_descriptor_used);
    assert(texture.bindless_image_info.imageView == 999);
    unsigned previous_writes = descriptor_writes;
    /* A newly created view has the same raw handle, not the same lifetime. */
    renderer.texture_bindings[1] = &retained;
    exercise_cached(&nv2a.pgraph, 1, &surface, &retained, true);
    check_unit(1);
    assert(descriptor_writes == previous_writes + 1);
    assert(!finishes && !flushes);
#endif
}

int main(void)
{
    for (unsigned bindless = 0; bindless <= OPT_BINDLESS_TEXTURES; bindless++) {
        shared_units(false, bindless);
        shared_units(true, bindless);
        zero_stamp(bindless);
        depth_attachment_rebind(bindless);
        normal_transition(bindless);
        changed_view(bindless);
    }
    blocked_driver();
    fast_path_revalidation();
    destroyed_view_reuse();
    printf("surface reuse: %u scenarios passed (bindless compiled=%d)\n",
           6 * (OPT_BINDLESS_TEXTURES + 1) + 2 + OPT_BINDLESS_TEXTURES,
           OPT_BINDLESS_TEXTURES);
    return 0;
}
