/*
 * Exercise production vertex sync footprints through actual RAM uploads.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stddef.h>
#include <stdint.h>

#define TARGET_PAGE_BITS 12
#define TARGET_PAGE_SIZE (1 << TARGET_PAGE_BITS)
#define TARGET_PAGE_MASK (~(uint64_t)(TARGET_PAGE_SIZE - 1))
#define TARGET_PAGE_ALIGN(a) ROUND_UP(a, TARGET_PAGE_SIZE)
#define ROUND_UP(a, b) (((a) + (b) - 1) & ~((uint64_t)(b) - 1))
#define DIV_ROUND_UP(a, b) (((a) + (b) - 1) / (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define PAGE_COUNT 8
#define MEMORY_SIZE (PAGE_COUNT * TARGET_PAGE_SIZE)
#define BUFFER_VERTEX_RAM 0
#define VK_FINISH_REASON_VERTEX_BUFFER_DIRTY 0
#define NV2A_PROF_GEOM_BUFFER_UPDATE_1 0
#define DIRTY_MEMORY_NV2A 0
#define DIRTY_MEMORY_BLOCK_SIZE PAGE_COUNT
#define NV2A_PERF_LOG 0
#define RCU_READ_LOCK_GUARD() ((void)0)
#define qatomic_rcu_read(p) (*(p))
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define VERTEX_SYNC_TEST

typedef uint64_t ram_addr_t;
typedef uint64_t VkDeviceSize;
typedef struct StorageBuffer {
    void *mapped;
    VkDeviceSize buffer_size;
} StorageBuffer;
typedef struct FrameStagingState {
    StorageBuffer vertex_ram;
    VkDeviceSize vertex_ram_flush_min, vertex_ram_flush_max;
    VkDeviceSize vertex_ram_propagate_min, vertex_ram_propagate_max;
    unsigned long uploaded_bitmap[1];
    unsigned long vertex_ram_stale[1];
    int vertex_ram_initialized;
} FrameStagingState;

/* Reuse the binder fixture's types, DMA stub and actual binder extraction. */
#define main vertex_cache_test_main
#include "vertex-cache-test.c"
#undef main

typedef struct DirtyMemoryBlocks {
    unsigned long *blocks[1];
} DirtyMemoryBlocks;
typedef struct VsyncTimingWork {
    unsigned calls, reqs, merged, dirty_count;
    uint64_t bytes_copied;
} VsyncTimingWork;
static struct { VsyncTimingWork vsync_working; } g_nv2a_stats;
static struct { DirtyMemoryBlocks *dirty_memory[1]; } ram_list;
static uint8_t uploaded[MEMORY_SIZE];
static unsigned long dirty_pages, read_pages;
static DirtyMemoryBlocks dirty_blocks = { .blocks = { &dirty_pages } };
static unsigned finishes;

static size_t find_next_bit(const unsigned long *map, size_t end, size_t bit)
{
    assert(end <= PAGE_COUNT);
    while (bit < end && !(*map & (1UL << bit))) {
        bit++;
    }
    return bit;
}

static size_t find_next_zero_bit(const unsigned long *map, size_t end,
                                 size_t bit)
{
    assert(end <= PAGE_COUNT);
    while (bit < end && (*map & (1UL << bit))) {
        bit++;
    }
    return bit;
}

static void bitmap_set(unsigned long *map, size_t first, size_t count)
{
    assert(first + count <= PAGE_COUNT);
    for (size_t bit = first; bit < first + count; bit++) {
        *map |= 1UL << bit;
    }
}

static bool bitmap_test_and_clear_atomic(unsigned long *map, size_t first,
                                         size_t count)
{
    unsigned long mask = 0;
    bitmap_set(&mask, first, count);
    bool dirty = *map & mask;
    *map &= ~mask;
    return dirty;
}

static void physical_memory_dirty_bits_cleared(ram_addr_t start, uint64_t size)
{
    assert(start + size <= MEMORY_SIZE);
}

static unsigned long *get_uploaded_bitmap(PGRAPHVkState *r)
{
    return r->frame_staging[r->current_frame].uploaded_bitmap;
}

static StorageBuffer *get_staging_buffer(PGRAPHVkState *r, unsigned buffer)
{
    assert(buffer == BUFFER_VERTEX_RAM);
    return &r->frame_staging[r->current_frame].vertex_ram;
}

static void pgraph_vk_finish(PGRAPHState *pg, unsigned reason)
{
    (void)reason;
    finishes++;
    *pg->vk_renderer_state->cb_vertex_pages = 0;
}

static void pgraph_vk_download_surfaces_in_range_if_dirty(PGRAPHState *pg,
                                                         hwaddr offset,
                                                         VkDeviceSize size)
{
    (void)pg;
    assert(offset + size <= MEMORY_SIZE);
}

#include "vertex-sync-functions.inc"

static void sync_reset(Environment *e, unsigned format, unsigned count,
                        unsigned width, unsigned stride, unsigned base)
{
    reset(e, format);
    memset(e->memory, 'B', sizeof(e->memory));
    memset(uploaded, 'A', sizeof(uploaded));
    memset(&g_nv2a_stats, 0, sizeof(g_nv2a_stats));
    ram_list.dirty_memory[0] = &dirty_blocks;
    PGRAPHVkState *r = &e->renderer;
    r->current_frame = 0;
    r->num_active_frames = 1;
    r->bitmap_size = PAGE_COUNT;
    r->cb_vertex_pages = &read_pages;
    r->frame_staging[0].vertex_ram = (StorageBuffer) {
        .mapped = uploaded, .buffer_size = sizeof(uploaded),
    };
    r->frame_staging[0].vertex_ram_initialized = true;
    r->frame_staging[0].vertex_ram_flush_min = UINT64_MAX;
    *get_uploaded_bitmap(r) = (1UL << PAGE_COUNT) - 1;
    VertexAttribute *a = &e->nv2a.pgraph.vertex_attributes[0];
    *a = (VertexAttribute) {
        .format = format, .count = count, .size = width / count,
        .stride = stride, .offset = base,
    };
    e->nv2a.pgraph.vertex_attributes[3].count = 0;
    dirty_pages = read_pages = 0;
    finishes = 0;
}

static void bind_and_sync(Environment *e, unsigned min, unsigned max,
                           bool cached)
{
    if (cached) {
        bind_range(e, min, max, min, false);
        assert(e->renderer.cached_num_active_bindings > 0);
        e->renderer.num_vertex_ram_buffer_syncs = 0;
    }
    bind_range(e, min, max, min, false);
    for (unsigned i = 0;
         i < e->renderer.num_active_vertex_attribute_descriptions; i++) {
        const VkVertexInputAttributeDescription *desc =
            &e->renderer.vertex_attribute_descriptions[i];
        const VertexAttribute *attr =
            &e->nv2a.pgraph.vertex_attributes[desc->location];
        size_t size, count;
        get_size_and_count_for_format(desc->format, &size, &count);
        assert(size * count == attr->size * attr->count);
    }
    sync_vertex_ram_buffer(&e->nv2a.pgraph);
}

static void check_uploaded(const Environment *e, unsigned start, unsigned end)
{
    if (memcmp(uploaded + start, e->memory + start, end - start)) {
        fputs("vertex sync omitted a consumed tail byte\n", stderr);
        abort();
    }
}

static void overlapping(unsigned format, unsigned count, unsigned width,
                         unsigned stride, bool cached, bool fallback)
{
    Environment e;
    unsigned base = 2 * TARGET_PAGE_SIZE - 2 * stride;
    sync_reset(&e, format, count, width, stride, base);
    e.renderer.sscaled_vertex_supported = !fallback;
    /* Only the tail page is dirty: an uploaded prefix alone cannot suffice. */
    memcpy(uploaded, e.memory, 2 * TARGET_PAGE_SIZE);
    dirty_pages = 1UL << 2;
    bind_and_sync(&e, 0, 1, cached);
    check_uploaded(&e, base, base + stride + width);
    assert(!dirty_pages);
    assert(g_nv2a_stats.vsync_working.dirty_count == 1);
    /* Repeated clean bindings must not copy or finish. */
    unsigned dirty_count = g_nv2a_stats.vsync_working.dirty_count;
    bind_and_sync(&e, 0, 1, true);
    assert(g_nv2a_stats.vsync_working.dirty_count == dirty_count);
    assert(!finishes);
}

static void padded_stride(bool cached)
{
    Environment e;
    unsigned base = 0x1fb0;
    sync_reset(&e, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 3, 12, 64, base);
    dirty_pages = (1UL << 1) | (1UL << 2);
    bind_and_sync(&e, 0, 1, cached);
    check_uploaded(&e, base, base + 64 + 12);
    assert(dirty_pages == (1UL << 2));
    assert(uploaded[2 * TARGET_PAGE_SIZE] == 'A');
    assert(g_nv2a_stats.vsync_working.bytes_copied == TARGET_PAGE_SIZE);
}

static void packed_format(bool cached)
{
    Environment e;
    unsigned base = 0x1ffc;
    sync_reset(&e, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP, 1, 4, 4, base);
    dirty_pages = (1UL << 1) | (1UL << 2);
    bind_and_sync(&e, 0, 1, cached);
    check_uploaded(&e, base, base + 8);
    assert(!dirty_pages);
}

static void final_memory_element(bool cached)
{
    Environment e;
    unsigned base = MEMORY_SIZE - 3 * 64 - 16;
    sync_reset(&e, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 4, 16, 64, base);
    dirty_pages = 1UL << (PAGE_COUNT - 1);
    bind_and_sync(&e, 2, 3, cached);
    check_uploaded(&e, base + 2 * 64, MEMORY_SIZE);
    assert(!dirty_pages);
}

static void zero_stride(void)
{
    Environment e;
    sync_reset(&e, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 4, 16, 0, 64);
    dirty_pages = 1;
    bind_range(&e, 2, 3, 2, false);
    assert(!e.renderer.num_vertex_ram_buffer_syncs);
    sync_vertex_ram_buffer(&e.nv2a.pgraph);
    assert(!g_nv2a_stats.vsync_working.calls && dirty_pages == 1);
    assert(!memcmp(e.nv2a.pgraph.vertex_attributes[0].inline_value,
                   e.memory + 64, 16));
}

int main(void)
{
    for (unsigned cached = 0; cached < 2; cached++) {
        overlapping(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 4, 16, 4,
                    cached, false);
        overlapping(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D, 4, 4, 1,
                    cached, false);
        overlapping(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL, 3, 3, 1,
                    cached, false);
        overlapping(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1, 4, 8, 2,
                    cached, false);
        overlapping(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K, 4, 8, 2,
                    cached, false);
        overlapping(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K, 4, 8, 2,
                    cached, true);
        padded_stride(cached);
        packed_format(cached);
        final_memory_element(cached);
    }
    zero_stride();
    puts("vertex sync footprint: 19 production-path scenarios passed");
    return 0;
}
