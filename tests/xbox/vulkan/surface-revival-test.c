/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Actual surface-cache lifecycle with deterministic transfer boundaries.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qemu/queue.h"

typedef uint64_t hwaddr;
typedef uint64_t ram_addr_t;
typedef void GHashTable;
typedef void *gpointer;
typedef struct SurfaceBinding SurfaceBinding;
typedef struct PGRAPHVkState PGRAPHVkState;

typedef struct SurfaceShape {
    unsigned anti_aliasing, clip_x, clip_y, clip_width, clip_height;
} SurfaceShape;
typedef struct Surface {
    bool buffer_dirty, draw_dirty, write_enabled_cache;
} Surface;
typedef struct PGRAPHState {
    PGRAPHVkState *vk_renderer_state;
    Surface surface_color, surface_zeta;
    SurfaceShape surface_shape;
    struct {
        unsigned width, height, clip_x, clip_y, clip_width, clip_height;
    } surface_binding_dim;
    bool clearing;
} PGRAPHState;
typedef struct NV2AState {
    PGRAPHState pgraph;
    uint8_t *vram_ptr;
    void *vram;
} NV2AState;
struct SurfaceBinding {
    QTAILQ_ENTRY(SurfaceBinding) entry;
    SurfaceShape shape;
    struct { unsigned vk_format; } host_fmt;
    bool color, swizzle, draw_dirty, download_pending, upload_pending;
    bool initialized, callback_registered;
    unsigned width, height, pitch, size;
    unsigned download_row_count, draw_generation, download_generation;
    int invalidation_frame, retire_frame;
    hwaddr vram_addr;
    uintptr_t image, image_view, allocation, image_scratch;
    uintptr_t allocation_scratch, image_layout;
    uintptr_t image_scratch_current_layout;
};
typedef QTAILQ_HEAD(, SurfaceBinding) SurfaceList;
struct PGRAPHVkState {
    SurfaceList surfaces, shelved_surfaces;
    SurfaceBinding *color_binding, *zeta_binding;
    bool in_render_pass, in_command_buffer;
    unsigned surface_list_gen;
    ram_addr_t vram_ram_addr;
};
typedef struct DirtyMemoryBlocks { unsigned long *blocks[1]; }
    DirtyMemoryBlocks;
static struct { DirtyMemoryBlocks *dirty_memory[1]; } ram_list;
static struct {
    struct {
        unsigned update_calls, evict_count, create_count, hit_count, nop_count;
        unsigned download_count;
        int64_t populate_ns, dirty_ns, enrp_ns, lk_hit_ns, lk_evict_ns;
        int64_t lk_nosurf_ns, create_ns, put_ns, bind_ns, download_ns;
    } surf_working;
} g_nv2a_stats;

#define TARGET_PAGE_BITS 12
#define TARGET_PAGE_ALIGN(v) (((v) + 4095) & ~(hwaddr)4095)
#define DIRTY_MEMORY_BLOCK_SIZE 64
#define DIRTY_MEMORY_NV2A 0
#define DIRTY_MEMORY_NV2A_TEX 1
#define DIRTY_MEMORY_VGA 2
#define VK_NULL_HANDLE 0
#define VK_IMAGE_LAYOUT_UNDEFINED 0
#define VK_FINISH_REASON_SURFACE_DOWN 1
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define RCU_READ_LOCK_GUARD() ((void)0)
#define qatomic_rcu_read(ptr) (*(ptr))
#define OPT_STAT_INC(name) ((void)0)
#define VK_LOG(...) ((void)0)
#define NV2A_UNIMPLEMENTED(...) abort()
#define g_malloc(size) malloc(size)

static SurfaceBinding requested;
static uint8_t vram[256], images[4096][256];
static uintptr_t next_image;
static unsigned downloads, uploads, callbacks_removed, finishes;
static unsigned dirty_notifications[3];

static int64_t nv2a_clock_ns(void) { return 0; }
static bool tcg_enabled(void) { return true; }
static bool bitmap_test_and_clear_atomic(unsigned long *p, unsigned ofs,
                                         unsigned count)
{
    abort();
}
static void trace_surface(uint32_t addr, uint32_t width, uint32_t height,
                          const char *layout, uint32_t aa, uint32_t x,
                          uint32_t w, uint32_t y, uint32_t h, uint32_t pitch) {}
#define trace_nv2a_pgraph_surface_match_color trace_surface
#define trace_nv2a_pgraph_surface_match_zeta trace_surface
#define trace_nv2a_pgraph_surface_create_color trace_surface
#define trace_nv2a_pgraph_surface_create_zeta trace_surface
#define trace_nv2a_pgraph_surface_hit_color trace_surface
#define trace_nv2a_pgraph_surface_hit_zeta trace_surface
#define trace_nv2a_pgraph_surface_target(...) ((void)0)
#define trace_nv2a_pgraph_surface_evict_reason(...) ((void)0)
static void populate_surface_binding_target(NV2AState *d, bool color,
                                            SurfaceBinding *target)
{
    *target = requested;
    target->color = color;
    target->upload_pending = true;
    target->initialized = false;
}
static void populate_surface_binding_target_sized(NV2AState *d, bool color,
                                                  unsigned w, unsigned h,
                                                  SurfaceBinding *target)
{
    populate_surface_binding_target(d, color, target);
    target->width = w;
    target->height = h;
    target->vram_addr = 1024;
}
static void pgraph_vk_ensure_not_in_render_pass(PGRAPHState *pg)
{
    pg->vk_renderer_state->in_render_pass = false;
}
static void unbind_surface(NV2AState *d, bool color)
{
    if (color) {
        d->pgraph.vk_renderer_state->color_binding = NULL;
    } else {
        d->pgraph.vk_renderer_state->zeta_binding = NULL;
    }
}
static SurfaceBinding *pgraph_vk_surface_get(NV2AState *d, hwaddr addr)
{
    SurfaceBinding *surface;
    QTAILQ_FOREACH(surface, &d->pgraph.vk_renderer_state->surfaces, entry) {
        if (surface->vram_addr == addr) {
            return surface;
        }
    }
    return NULL;
}
static bool check_surface_compatibility(SurfaceBinding *a, SurfaceBinding *b,
                                         bool strict)
{
    return a->color == b->color && a->width == b->width &&
           a->height == b->height && a->pitch == b->pitch &&
           a->host_fmt.vk_format == b->host_fmt.vk_format;
}
static void compare_surfaces(SurfaceBinding *a, SurfaceBinding *b) {}
static void pgraph_vk_finish(PGRAPHState *pg, int reason) { finishes++; }
static void download_surface_to_buffer(NV2AState *d, SurfaceBinding *s,
                                       uint8_t *pixels)
{
    assert(s->callback_registered);
    memcpy(pixels, images[s->image], s->size);
    downloads++;
}
static void memory_region_set_client_dirty(void *mr, hwaddr addr,
                                           unsigned size, int client)
{
    dirty_notifications[client]++;
}
static void download_surface(NV2AState *d, SurfaceBinding *s, bool force);
static void pgraph_vk_surface_download_if_dirty(NV2AState *d, SurfaceBinding *s)
{
    if (s->draw_dirty) {
        download_surface(d, s, true);
    }
}
static void unregister_cpu_access_callback(NV2AState *d, SurfaceBinding *s)
{
    assert(s->callback_registered);
    s->callback_registered = false;
    callbacks_removed++;
}
static GHashTable *surface_addr_map_get(PGRAPHVkState *r, const char *where,
                                       bool create)
{
    return NULL;
}
static void g_hash_table_remove(GHashTable *table, void *key) { abort(); }
static SurfaceBinding *get_any_compatible_invalid_surface(PGRAPHVkState *r,
                                                          SurfaceBinding *s)
{
    return NULL;
}
static void create_surface_image(PGRAPHState *pg, SurfaceBinding *s)
{
    assert(next_image + 1 < 4096);
    s->image = ++next_image;
    s->image_view = s->image + 10000;
    s->allocation = s->image + 20000;
}
static void set_surface_label(PGRAPHState *pg, SurfaceBinding *s) {}
static void surface_put(NV2AState *d, SurfaceBinding *s)
{
    s->callback_registered = true;
    QTAILQ_INSERT_HEAD(&d->pgraph.vk_renderer_state->surfaces, s, entry);
}
static void bind_surface(PGRAPHVkState *r, SurfaceBinding *s)
{
    if (s->color) {
        r->color_binding = s;
    } else {
        r->zeta_binding = s;
    }
}
static void download_surface_deferred(NV2AState *d, SurfaceBinding *s)
{
    abort();
}

#include "surface-revival-functions.inc"

static SurfaceBinding *update_and_transfer(NV2AState *d, bool color)
{
    Surface *part = color ? &d->pgraph.surface_color : &d->pgraph.surface_zeta;
    part->buffer_dirty = true;
    update_surface_part(d, true, color);
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;
    SurfaceBinding *s = color ? r->color_binding : r->zeta_binding;
    assert(s && s->callback_registered);
    /* The transfer boundary preserves production's pending-upload contract. */
    if (s->upload_pending) {
        assert(!s->initialized);
        memcpy(images[s->image], d->vram_ptr + s->vram_addr, s->size);
        s->upload_pending = false;
        s->initialized = true;
        uploads++;
    }
    return s;
}

static void run_lifecycle(bool color, unsigned cpu_write, bool dirty)
{
    PGRAPHVkState r = { 0 };
    NV2AState d = { .pgraph.vk_renderer_state = &r, .vram_ptr = vram };
    QTAILQ_INIT(&r.surfaces);
    QTAILQ_INIT(&r.shelved_surfaces);
    requested = (SurfaceBinding) {
        .color = color, .width = 4, .height = 4, .pitch = 16, .size = 64,
        .host_fmt.vk_format = 1,
    };
    memset(vram, 0x13, sizeof(vram));
    unsigned before_downloads = downloads, before_removed = callbacks_removed;
    SurfaceBinding *first = update_and_transfer(&d, color);
    uintptr_t image = first->image, view = first->image_view;
    if (dirty) {
        /* A partial GPU draw must survive the download before shelving. */
        memset(images[image] + 7, 0x38, 21);
        first->draw_dirty = true;
        first->draw_generation++;
    }
    uint8_t expected[64];
    memcpy(expected, images[image], sizeof(expected));

    requested.host_fmt.vk_format = 2;
    SurfaceBinding *second = update_and_transfer(&d, color);
    assert(second != first && !first->callback_registered);
    assert(callbacks_removed == before_removed + 1);
    assert(downloads == before_downloads + dirty);
    assert(!memcmp(vram, expected, sizeof(expected)));
    if (cpu_write) {
        /* No callback can set upload_pending while the source is shelved. */
        assert(!first->upload_pending);
        if (cpu_write == 1) {
            memset(vram, 0xa7, first->size);
        } else {
            memset(vram + 11, 0xa7, 9);
            memset(vram + 57, 0x6c, 3);
        }
    }

    requested.host_fmt.vk_format = 1;
    unsigned before_uploads = uploads;
    SurfaceBinding *revived = update_and_transfer(&d, color);
    if (uploads != before_uploads + 1 ||
        memcmp(images[revived->image], vram, revived->size)) {
        fputs("revived surface skipped authoritative VRAM upload\n", stderr);
        exit(1);
    }
    assert(revived == first && revived->image == image);
    assert(revived->image_view == view && revived->initialized);
    assert(revived->callback_registered && !second->callback_registered);
    assert(next_image % 2 == 0);

    /* Partial clears must preserve the refreshed pixels outside their box. */
    memcpy(expected, vram, sizeof(expected));
    memset(images[image] + 12, 0x51, 6);
    memset(expected + 12, 0x51, 6);
    assert(!memcmp(images[image], expected, sizeof(expected)));

    /* An active compatible cache hit keeps its existing no-upload behavior. */
    before_uploads = uploads;
    assert(update_and_transfer(&d, color) == revived);
    assert(uploads == before_uploads);
    QTAILQ_REMOVE(&r.surfaces, first, entry);
    QTAILQ_REMOVE(&r.shelved_surfaces, second, entry);
    free(first);
    free(second);
}

static void test_shelf_matching(void)
{
    PGRAPHVkState r = { 0 };
    SurfaceBinding s = {
        .vram_addr = 128, .width = 4, .height = 4, .pitch = 16,
        .host_fmt.vk_format = 1, .color = true,
    };
    QTAILQ_INIT(&r.shelved_surfaces);
    QTAILQ_INSERT_HEAD(&r.shelved_surfaces, &s, entry);
    for (unsigned i = 0; i < 6; i++) {
        SurfaceBinding mismatch = s;
        switch (i) {
        case 0:
            mismatch.vram_addr++;
            break;
        case 1:
            mismatch.width++;
            break;
        case 2:
            mismatch.height++;
            break;
        case 3:
            mismatch.pitch++;
            break;
        case 4:
            mismatch.host_fmt.vk_format++;
            break;
        case 5:
            mismatch.color = false;
            break;
        }
        assert(!get_shelved_surface(&r, mismatch.vram_addr, &mismatch));
        assert(QTAILQ_FIRST(&r.shelved_surfaces) == &s);
    }
    assert(get_shelved_surface(&r, s.vram_addr, &s) == &s);
    assert(QTAILQ_EMPTY(&r.shelved_surfaces));
}

int main(void)
{
    test_shelf_matching();
    for (unsigned i = 0; i < 1536; i++) {
        run_lifecycle(i & 1, 2 - (i / 4) % 3, !(i & 2));
    }
    assert(downloads == 768 && callbacks_removed == 3072);
    assert(dirty_notifications[DIRTY_MEMORY_NV2A_TEX] == downloads);
    assert(dirty_notifications[DIRTY_MEMORY_VGA] == downloads);
    assert(finishes == 0);
    puts("surface revival: 1536 color/depth, dirty/clean, full/partial/no CPU "
         "write lifecycles and six key mismatch guards passed; allocation "
         "reuse, partial-clear pixels and active-cache hits preserved");
    return 0;
}
