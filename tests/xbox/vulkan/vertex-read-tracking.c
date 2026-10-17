/*
 * Deferred vertex-read lifetime regression, without Vulkan or game files.
 * The runner includes extracted production bind/snapshot/upload functions.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define DIV_ROUND_UP(a, b) (((a) + (b) - 1) / (b))
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define TARGET_PAGE_SIZE 4096
#define TARGET_PAGE_ALIGN(a) (((a) + TARGET_PAGE_SIZE - 1) & \
                             ~((uint64_t)TARGET_PAGE_SIZE - 1))
#define PAGE_COUNT 8
#define MEMORY_SIZE (PAGE_COUNT * TARGET_PAGE_SIZE)
#define REORDER_WINDOW_MAX 4
#define REORDER_MAX_PIPELINES 4
#define BUFFER_VERTEX_RAM 0
#define BUFFER_VERTEX_INLINE 1
#define VK_FINISH_REASON_VERTEX_BUFFER_DIRTY 0
#define NV2A_PROF_GEOM_BUFFER_UPDATE_1 0
#define NV2A_PERF_LOG 0

typedef uint64_t hwaddr;
typedef uint64_t VkDeviceSize;
typedef uintptr_t VkBuffer;
typedef unsigned VkFormat;
typedef struct VkVertexInputAttributeDescription {
    unsigned location, binding;
    VkFormat format;
    unsigned offset;
} VkVertexInputAttributeDescription;
typedef struct VkVertexInputBindingDescription {
    unsigned binding, stride, inputRate;
} VkVertexInputBindingDescription;
typedef struct StorageBuffer {
    uint8_t *mapped;
    VkBuffer buffer;
    VkDeviceSize buffer_size;
} StorageBuffer;
typedef struct FrameStagingState {
    StorageBuffer vertex_ram;
    VkDeviceSize vertex_ram_flush_min, vertex_ram_flush_max;
    VkDeviceSize vertex_ram_propagate_min, vertex_ram_propagate_max;
    unsigned long *vertex_ram_stale;
    bool vertex_ram_initialized;
    unsigned long uploaded_bitmap[1];
} FrameStagingState;
typedef struct ReorderWindowEntry {
    int num_vertex_bindings;
    VkBuffer vertex_buffers[16];
    VkDeviceSize vertex_offsets[16];
    void *pipeline_binding;
    unsigned vertex_count;
    int sequence_number, group_order;
} ReorderWindowEntry;
typedef struct ReorderWindow {
    ReorderWindowEntry entries[REORDER_WINDOW_MAX];
    int count, num_seen_pipelines, next_group;
    bool active;
    void *seen_pipelines[REORDER_MAX_PIPELINES];
    int seen_pipeline_group[REORDER_MAX_PIPELINES];
} ReorderWindow;
typedef struct PGRAPHVkState {
    int current_frame;
    int num_active_frames;
    FrameStagingState frame_staging[1];
    StorageBuffer inline_buffer;
    unsigned long read_bitmap[1];
    unsigned long *cb_vertex_pages;
    size_t bitmap_size;
    int num_active_vertex_binding_descriptions;
    VkVertexInputAttributeDescription vertex_attribute_descriptions[16];
    VkVertexInputBindingDescription vertex_binding_descriptions[16];
    VkDeviceSize vertex_attribute_offsets[16];
    unsigned command_buffer;
    ReorderWindow reorder_window;
} PGRAPHVkState;
typedef struct PGRAPHState {
    PGRAPHVkState *vk_renderer_state;
    unsigned draw_arrays_length;
} PGRAPHState;
typedef struct NV2AState {
    PGRAPHState pgraph;
} NV2AState;

static PGRAPHVkState renderer;
static PGRAPHState pg = { .vk_renderer_state = &renderer };
static uint8_t mapped[MEMORY_SIZE], inline_mapped[MEMORY_SIZE];
static VkBuffer bound_buffers[16];
static VkDeviceSize bound_offsets[16];
static const uint8_t *pending[16];
static uint8_t retired[16];
static unsigned pending_count, retired_count, finish_count;
static bool finish_during_capture, capture_success;

static void bitmap_set(unsigned long *map, size_t first, size_t count)
{
    assert(first + count <= PAGE_COUNT);
    for (size_t bit = first; bit < first + count; bit++) {
        *map |= 1UL << bit;
    }
}

static size_t find_next_bit(const unsigned long *map, size_t end, size_t start)
{
    assert(end <= PAGE_COUNT);
    while (start < end && !(*map & (1UL << start))) {
        start++;
    }
    return start;
}

#ifndef NEGATIVE_CONTROL
static size_t find_next_zero_bit(const unsigned long *map, size_t end,
                                 size_t start)
{
    assert(end <= PAGE_COUNT);
    while (start < end && (*map & (1UL << start))) {
        start++;
    }
    return start;
}
#endif

static StorageBuffer *get_staging_buffer(PGRAPHVkState *r, int index)
{
    return index == BUFFER_VERTEX_RAM ?
           &r->frame_staging[r->current_frame].vertex_ram : &r->inline_buffer;
}

static unsigned long *get_uploaded_bitmap(PGRAPHVkState *r)
{
    return r->frame_staging[r->current_frame].uploaded_bitmap;
}

static void retire_draws(void)
{
    /* A command buffer stores buffer references, not copies of vertex bytes. */
    for (unsigned i = 0; i < pending_count; i++) {
        retired[retired_count++] = *pending[i];
    }
    pending_count = 0;
}

static void pgraph_vk_finish(PGRAPHState *state, int reason)
{
    (void)reason;
    finish_count++;
    /* Production finish emits queued reorder entries before clearing bits. */
    retire_draws();
    *state->vk_renderer_state->cb_vertex_pages = 0;
}

static void pgraph_vk_download_surfaces_in_range_if_dirty(PGRAPHState *state,
                                                         hwaddr addr,
                                                         VkDeviceSize size)
{
    (void)state;
    (void)addr;
    (void)size;
}

static void nv2a_profile_inc_counter(int counter)
{
    (void)counter;
}

static void vkCmdBindVertexBuffers(unsigned command_buffer, unsigned first,
                                   unsigned count, const VkBuffer *buffers,
                                   const VkDeviceSize *offsets)
{
    (void)command_buffer;
    assert(first == 0 && count <= ARRAY_SIZE(bound_buffers));
    memcpy(bound_buffers, buffers, count * sizeof(*buffers));
    memcpy(bound_offsets, offsets, count * sizeof(*offsets));
}

static void pgraph_vk_flush_draw_queue(NV2AState *d)
{
    (void)d;
}

static bool try_snapshot_draw_arrays(NV2AState *d, ReorderWindowEntry *entry)
{
    if (finish_during_capture) {
        ReorderWindow *w = &d->pgraph.vk_renderer_state->reorder_window;
        w->count = w->num_seen_pipelines = w->next_group = 0;
        w->active = false;
    }
    entry->vertex_count = 77;
    entry->pipeline_binding = &renderer;
    return capture_success;
}

static bool try_snapshot_inline_elements(NV2AState *d,
                                         ReorderWindowEntry *entry)
{
    bool ok = try_snapshot_draw_arrays(d, entry);
    entry->vertex_count = 88;
    return ok;
}

#include "vertex-read-functions.inc"

static void record_binding(uint16_t inline_map, VkDeviceSize offset,
                            uint32_t min_element, uint32_t max_element)
{
#ifdef NEGATIVE_CONTROL
    (void)min_element;
    (void)max_element;
    bind_vertex_buffer(&pg, inline_map, offset);
#else
    bind_vertex_buffer(&pg, inline_map, offset, min_element, max_element);
#endif
}

static void record_snapshot(ReorderWindowEntry *entry, uint16_t inline_map,
                             uint32_t min_element, uint32_t max_element)
{
#ifdef NEGATIVE_CONTROL
    (void)min_element;
    (void)max_element;
    snapshot_vertex_buffers(&pg, entry, inline_map, 0);
#else
    snapshot_vertex_buffers(&pg, entry, inline_map, 0, min_element,
                             max_element);
#endif
}

static void reset(void)
{
    memset(&renderer, 0, sizeof(renderer));
    memset(mapped, 'A', sizeof(mapped));
    memset(inline_mapped, 'I', sizeof(inline_mapped));
    renderer.cb_vertex_pages = renderer.read_bitmap;
    renderer.bitmap_size = PAGE_COUNT;
    renderer.frame_staging[0].vertex_ram = (StorageBuffer) {
        .mapped = mapped, .buffer = (uintptr_t)mapped,
        .buffer_size = sizeof(mapped),
    };
    renderer.inline_buffer = (StorageBuffer) {
        .mapped = inline_mapped, .buffer = (uintptr_t)inline_mapped,
        .buffer_size = sizeof(inline_mapped),
    };
    renderer.frame_staging[0].vertex_ram_flush_min = UINT64_MAX;
    renderer.frame_staging[0].vertex_ram_initialized = true;
    renderer.frame_staging[0].vertex_ram_propagate_min = UINT64_MAX;
    renderer.num_active_frames = 1;
    bitmap_set(get_uploaded_bitmap(&renderer), 0, PAGE_COUNT);
    pending_count = retired_count = finish_count = 0;
}

static void attribute(unsigned binding, unsigned location, VkDeviceSize base,
                       unsigned stride, VkFormat format)
{
    renderer.num_active_vertex_binding_descriptions = binding + 1;
    renderer.vertex_attribute_descriptions[binding] =
        (VkVertexInputAttributeDescription) {
            .location = location, .binding = binding, .format = format,
        };
    renderer.vertex_binding_descriptions[binding].stride = stride;
    renderer.vertex_attribute_offsets[location] = base;
}

static void queue_read(VkBuffer buffer, VkDeviceSize offset)
{
    assert(pending_count < ARRAY_SIZE(pending));
    pending[pending_count++] = (uint8_t *)(uintptr_t)buffer + offset;
}

static void overwrite(hwaddr offset)
{
    uint8_t replacement = 'B';
    pgraph_vk_update_vertex_ram_buffer(&pg, offset, &replacement, 1);
}

static void check_retained(unsigned index, uint8_t expected)
{
    retire_draws();
    if (retired[index] != expected) {
        fprintf(stderr, "recorded draw changed before submission: %c != %c\n",
                retired[index], expected);
        abort();
    }
}

static void clean_reuse(bool late_finish, bool snapshot)
{
    reset();
    attribute(0, 0, TARGET_PAGE_SIZE, 16, VK_FORMAT_R32G32B32A32_SFLOAT);
    if (late_finish) {
        /* Setup may finish after sync, before the final vertex binding. */
        bitmap_set(renderer.cb_vertex_pages, 1, 1);
        pgraph_vk_finish(&pg, 0);
        finish_count = 0;
    }
    if (snapshot) {
        ReorderWindowEntry entry;
        record_snapshot(&entry, 0, 0, 3);
        queue_read(entry.vertex_buffers[0], entry.vertex_offsets[0]);
    } else {
        /* Clean uploaded / range-skip paths do not call the upload function. */
        record_binding(0, 0, 0, 3);
        queue_read(bound_buffers[0], bound_offsets[0]);
    }
    overwrite(TARGET_PAGE_SIZE);
    check_retained(0, 'A');
    assert(finish_count == 1);
    assert(mapped[TARGET_PAGE_SIZE] == 'B');
}

static void unrelated_write(void)
{
    reset();
    attribute(0, 0, TARGET_PAGE_SIZE, 4, VK_FORMAT_R32_SFLOAT);
    record_binding(0, 0, 0, 4);
    queue_read(bound_buffers[0], bound_offsets[0]);
    overwrite(3 * TARGET_PAGE_SIZE);
    check_retained(0, 'A');
    assert(finish_count == 0);
}

static void exact_footprints(void)
{
    reset();
    /* The final vertex straddles a page even though stride < element width. */
    attribute(0, 0, TARGET_PAGE_SIZE - 32, 8,
              VK_FORMAT_R32G32B32A32_SFLOAT);
    attribute(1, 3, 3 * TARGET_PAGE_SIZE + 12, 12,
              VK_FORMAT_R32G32B32_SFLOAT);
    attribute(2, 1, 6 * TARGET_PAGE_SIZE, 16,
              VK_FORMAT_R32G32B32A32_SFLOAT);
    record_binding(1 << 1, 0, 2, 4);
    assert(*renderer.cb_vertex_pages == ((1UL << 0) | (1UL << 1) | (1UL << 3)));
    queue_read(bound_buffers[0], TARGET_PAGE_SIZE + 4);
    overwrite(TARGET_PAGE_SIZE + 4);
    check_retained(0, 'A');
    assert(finish_count == 1);

    reset();
    /* Include the bound offset, and avoid marking the unused leading page. */
    attribute(0, 5, 0, 16, VK_FORMAT_R32G32B32A32_SFLOAT);
    record_binding(0, TARGET_PAGE_SIZE - 32, 2, 3);
    assert(*renderer.cb_vertex_pages == (1UL << 1));
}

static void inline_only(void)
{
    reset();
    attribute(0, 0, TARGET_PAGE_SIZE, 16, VK_FORMAT_R32G32B32A32_SFLOAT);
    record_binding(0xffff, 0, 0, 4);
    assert(*renderer.cb_vertex_pages == 0);
    queue_read(bound_buffers[0], bound_offsets[0]);
    overwrite(TARGET_PAGE_SIZE);
    check_retained(0, 'I');
    assert(finish_count == 0);
}

static void reorder_admission(bool finish, bool indexed, bool success)
{
    reset();
    ReorderWindow *w = &renderer.reorder_window;
    w->count = 2;
    w->entries[0].vertex_count = 11;
    w->entries[1].vertex_count = 22;
    finish_during_capture = finish;
    capture_success = success;
    NV2AState d = { .pgraph = {
        .vk_renderer_state = &renderer,
        .draw_arrays_length = !indexed,
    } };
    exercise_admission(&d);
    int previous = finish ? 0 : 2;
    assert(w->count == previous + success);
    if (success && w->entries[previous].vertex_count != (indexed ? 88u : 77u)) {
        fputs("captured draw admitted into stale entry after finish\n", stderr);
        abort();
    }
}

int main(int argc, char **argv)
{
    assert(argc == 1 || (argc == 2 && !strcmp(argv[1], "admission")));
    if (argc == 1) {
        clean_reuse(false, false);
        clean_reuse(true, false);
        clean_reuse(false, true);
        clean_reuse(true, true);
        unrelated_write();
        exact_footprints();
        inline_only();
        puts("vertex read lifetime: 7 scenarios passed");
    }
    reorder_admission(false, false, true);
    reorder_admission(true, false, true);
    reorder_admission(true, true, true);
    reorder_admission(false, false, false);
    reorder_admission(true, false, false);
    puts("reorder admission: 5 scenarios passed");
    return 0;
}
