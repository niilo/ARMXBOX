/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Production helpers are checked against independent byte-array contents. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define DIV_ROUND_UP(a, b) (((a) + (b) - 1) / (b))
#define BITS_PER_LONG (8 * sizeof(unsigned long))
#define BITS_TO_LONGS(n) DIV_ROUND_UP(n, BITS_PER_LONG)
#define TARGET_PAGE_SIZE 4096
#define TARGET_PAGE_ALIGN(a) (((a) + TARGET_PAGE_SIZE - 1) & \
                             ~((uint64_t)TARGET_PAGE_SIZE - 1))
#define NUM_SUBMIT_FRAMES 3
#define VK_WHOLE_SIZE UINT64_MAX
#define VK_TRUE 1
#define VK_CHECK(result) assert((result) == 0)
#define BUFFER_VERTEX_RAM 0
#define VK_FINISH_REASON_VERTEX_BUFFER_DIRTY 0
#define NV2A_PROF_GEOM_BUFFER_UPDATE_1 0
#define g_free free

typedef uint64_t hwaddr;
typedef uint64_t VkDeviceSize;
typedef int Error;
typedef struct StorageBuffer {
    uint8_t *mapped;
    VkDeviceSize buffer_size, buffer_offset;
} StorageBuffer;
typedef struct FrameStagingState {
    StorageBuffer vertex_ram, index_staging, vertex_inline_staging;
    StorageBuffer uniform_staging, staging_src;
    VkDeviceSize vertex_ram_flush_min, vertex_ram_flush_max;
    bool vertex_ram_initialized;
    unsigned long *uploaded_bitmap, *vertex_ram_stale;
#if defined(NEGATIVE_CONTROL) || defined(UPLOAD_NEGATIVE_CONTROL)
    VkDeviceSize vertex_ram_propagate_min, vertex_ram_propagate_max;
#endif
} FrameStagingState;
typedef struct PGRAPHVkState {
    FrameStagingState frame_staging[NUM_SUBMIT_FRAMES];
    int current_frame, num_active_frames;
    size_t bitmap_size;
    unsigned long *cb_vertex_pages;
    bool frame_enqueued[NUM_SUBMIT_FRAMES];
    bool frame_submitted[NUM_SUBMIT_FRAMES];
    int frame_fences[NUM_SUBMIT_FRAMES], device;
    int deferred_framebuffer_count[NUM_SUBMIT_FRAMES];
    int deferred_framebuffers[NUM_SUBMIT_FRAMES][2];
    int descriptor_set_index, push_ubo_set_index;
} PGRAPHVkState;
typedef struct PGRAPHState {
    PGRAPHVkState *vk_renderer_state;
} PGRAPHState;
typedef struct RenderCommand {
    struct {
        hwaddr offset;
        VkDeviceSize size;
        void *data;
    } vertex_ram;
} RenderCommand;

#include "propagation-types.inc"
static struct {
    VertexRamTimingWork vram_working;
} g_nv2a_stats;
static PGRAPHVkState renderer;
static PGRAPHState pg = { &renderer };
static uint8_t *latest;
static size_t total_bytes;
static uint64_t mirror_bytes, mirror_runs, clock_calls, last_copy_size;
static unsigned finish_calls;
static bool verify_suspended;
static bool download_rotate;
static uint8_t *download_source;
static uint8_t *finish_source;
#ifndef NEGATIVE_CONTROL
static unsigned flush_waits, idle_waits;
static int allocation_call, fail_allocation;
static int g_xemu_submit_frames = 3;
#endif

static void bitmap_set(unsigned long *map, size_t first, size_t count)
{
    assert(map && first + count <= renderer.bitmap_size);
    while (count--) {
        size_t bit = first++;
        map[bit / BITS_PER_LONG] |= 1UL << (bit % BITS_PER_LONG);
    }
}

static void bitmap_clear(unsigned long *map, size_t first, size_t count)
{
    assert(map && first + count <= renderer.bitmap_size);
    while (count--) {
        size_t bit = first++;
        map[bit / BITS_PER_LONG] &= ~(1UL << (bit % BITS_PER_LONG));
    }
}

static bool bit_set(const unsigned long *map, size_t bit)
{
    return (map[bit / BITS_PER_LONG] >> (bit % BITS_PER_LONG)) & 1;
}

static size_t find_next_bit(const unsigned long *map, size_t end, size_t first)
{
    assert(end <= renderer.bitmap_size);
    while (first < end && !bit_set(map, first)) {
        first++;
    }
    return first;
}

#ifndef NEGATIVE_CONTROL
static size_t find_next_zero_bit(const unsigned long *map, size_t end,
                                 size_t first)
{
    assert(end <= renderer.bitmap_size);
    while (first < end && bit_set(map, first)) {
        first++;
    }
    return first;
}

static unsigned long *bitmap_new(size_t bits)
{
    if (++allocation_call == fail_allocation) {
        return NULL;
    }
    return calloc(BITS_TO_LONGS(bits), sizeof(unsigned long));
}

static void error_setg(Error **errp, const char *message)
{
    static Error allocation_error;
    assert(strstr(message, "bitmaps"));
    if (errp) {
        *errp = &allocation_error;
    }
}
#endif

#if NV2A_PERF_LOG
static int64_t nv2a_clock_ns(void)
{
    return 100 * ++clock_calls;
}
#endif

static StorageBuffer *get_staging_buffer(PGRAPHVkState *r, int index)
{
    assert(index == BUFFER_VERTEX_RAM);
    return &r->frame_staging[r->current_frame].vertex_ram;
}

#if defined(NEGATIVE_CONTROL) || defined(UPLOAD_NEGATIVE_CONTROL)
static unsigned long *get_uploaded_bitmap(PGRAPHVkState *r)
{
    return r->frame_staging[r->current_frame].uploaded_bitmap;
}
#endif

static void rotate(void);

static bool pgraph_vk_download_surfaces_in_range_if_dirty(PGRAPHState *state,
                                                         hwaddr addr,
                                                         hwaddr size)
{
    assert(state == &pg);
    if (download_rotate) {
        download_rotate = false;
        rotate();
        bitmap_set(renderer.cb_vertex_pages, 0, 1);
    }
    if (download_source) {
        download_source[addr + size - 1] ^= 0x7a;
        download_source = NULL;
    }
    return false;
}

static void nv2a_profile_inc_counter(int counter)
{
    assert(counter == NV2A_PROF_GEOM_BUFFER_UPDATE_1);
}

static void pgraph_vk_finish(PGRAPHState *state, int reason)
{
    assert(state == &pg && reason == VK_FINISH_REASON_VERTEX_BUFFER_DIRTY);
    finish_calls++;
    rotate();
    if (finish_source) {
        finish_source[5] ^= 0x27;
        finish_source = NULL;
    }
}

#ifndef NEGATIVE_CONTROL
static void pgraph_vk_render_thread_wait_idle(PGRAPHVkState *r)
{
    assert(r == &renderer);
    idle_waits++;
}

static int vkWaitForFences(int device, unsigned count, const int *fences,
                          unsigned wait_all, uint64_t timeout)
{
    assert(device == 0 && count == 1 && *fences >= 0);
    assert(wait_all == VK_TRUE && timeout == UINT64_MAX);
    flush_waits++;
    return 0;
}

static void gpu_ts_readback(PGRAPHVkState *r, int frame)
{
    assert(r == &renderer && frame < r->num_active_frames);
}

static void vkDestroyFramebuffer(int device, int framebuffer, void *allocator)
{
    assert(device == 0 && framebuffer == 42 && !allocator);
}

static void pgraph_vk_reclaim_descriptor_overflow(PGRAPHVkState *r)
{
    assert(r == &renderer);
}

static void pgraph_vk_invalidate_desc_set_cache(PGRAPHVkState *r)
{
    assert(r == &renderer);
}

static void pgraph_vk_compute_finish_complete(PGRAPHVkState *r)
{
    assert(r == &renderer);
}
#endif

static void *observed_memcpy(void *dest, const void *source, size_t size)
{
    for (int i = 0; i < renderer.num_active_frames; i++) {
        uintptr_t start =
            (uintptr_t)renderer.frame_staging[i].vertex_ram.mapped;
        uintptr_t address = (uintptr_t)dest;
        if (address >= start && address < start + total_bytes) {
            assert(size <= start + total_bytes - address);
            if (i == renderer.current_frame && size) {
                size_t first = (address - start) / TARGET_PAGE_SIZE;
                size_t end = DIV_ROUND_UP(address - start + size,
                                          TARGET_PAGE_SIZE);
                assert(find_next_bit(renderer.cb_vertex_pages, end, first)
                       == end);
            }
            mirror_bytes += size;
            mirror_runs++;
            last_copy_size = size;
        }
    }
    return memcpy(dest, source, size);
}

#define memcpy observed_memcpy
#ifdef NEGATIVE_CONTROL
#include "propagation-original.inc"
#elif defined(UPLOAD_NEGATIVE_CONTROL)
#include "propagation-upload-original.inc"
#elif defined(UNRESTORED_NEGATIVE_CONTROL)
#include "propagation-unrestored.inc"
#else
#include "propagation-functions.inc"
#endif
#undef memcpy

static void verify(void)
{
    FrameStagingState *current =
        &renderer.frame_staging[renderer.current_frame];
    if (verify_suspended) {
        return;
    }
    assert(current->vertex_ram_initialized);
    if (memcmp(current->vertex_ram.mapped, latest, total_bytes)) {
#ifdef NEGATIVE_CONTROL
        fprintf(stderr, "original propagation lost multi-generation history\n");
        exit(1);
#else
        assert(!"current mirror differs from independent byte oracle");
#endif
    }
#ifndef NEGATIVE_CONTROL
    for (int i = 0; i < renderer.num_active_frames; i++) {
        FrameStagingState *fs = &renderer.frame_staging[i];
        if (!fs->vertex_ram_initialized) {
            continue;
        }
        for (size_t page = 0; page < renderer.bitmap_size; page++) {
            if (!bit_set(fs->vertex_ram_stale, page)) {
                size_t start = page * TARGET_PAGE_SIZE;
                size_t length = MIN(TARGET_PAGE_SIZE, total_bytes - start);
                assert(!memcmp(fs->vertex_ram.mapped + start,
                               latest + start, length));
            }
        }
    }
#endif
}

static void rotate(void)
{
    bitmap_clear(renderer.cb_vertex_pages, 0, renderer.bitmap_size);
    int next = (renderer.current_frame + 1) % renderer.num_active_frames;
#ifdef NEGATIVE_CONTROL
    original_propagate(&renderer, next);
#else
    pgraph_vk_propagate_vertex_ram(&renderer, next);
#endif
    renderer.current_frame = next;
    verify();
}

static void init(int depth, size_t bytes)
{
    memset(&renderer, 0, sizeof(renderer));
    memset(&g_nv2a_stats, 0, sizeof(g_nv2a_stats));
    total_bytes = bytes;
    renderer.bitmap_size = DIV_ROUND_UP(bytes, TARGET_PAGE_SIZE);
#ifdef NEGATIVE_CONTROL
    renderer.num_active_frames = depth;
#else
    xemu_set_submit_frames(depth);
    capture_frame_depth(&renderer);
#endif
    latest = calloc(bytes, 1);
    assert(latest);
    renderer.cb_vertex_pages = calloc(BITS_TO_LONGS(renderer.bitmap_size),
                                      sizeof(unsigned long));
    assert(renderer.cb_vertex_pages);
    for (int i = 0; i < renderer.num_active_frames; i++) {
        FrameStagingState *fs = &renderer.frame_staging[i];
        fs->vertex_ram = (StorageBuffer){ malloc(bytes), bytes, 0 };
        assert(fs->vertex_ram.mapped);
        memset(fs->vertex_ram.mapped, 0xcc, bytes);
#ifdef NEGATIVE_CONTROL
        fs->uploaded_bitmap = calloc(BITS_TO_LONGS(renderer.bitmap_size),
                                     sizeof(unsigned long));
        fs->vertex_ram_propagate_min = VK_WHOLE_SIZE;
#else
        Error *error = NULL;
        assert(pgraph_vk_init_vertex_ram_state(&renderer, fs, &error));
        assert(!error);
#endif
    }
    /* Production renderer.c uploads all VRAM, then marks slot 0 initialized. */
    pgraph_vk_update_vertex_ram_buffer(&pg, 0, latest, bytes);
    renderer.frame_staging[0].vertex_ram_initialized = true;
    verify();
    mirror_bytes = mirror_runs = clock_calls = 0;
}

static void cleanup(void)
{
    for (int i = 0; i < renderer.num_active_frames; i++) {
        FrameStagingState *fs = &renderer.frame_staging[i];
#ifdef NEGATIVE_CONTROL
        free(fs->uploaded_bitmap);
#else
        pgraph_vk_finalize_vertex_ram_state(fs);
        assert(!fs->uploaded_bitmap && !fs->vertex_ram_stale);
        assert(!fs->vertex_ram_initialized);
        assert(fs->vertex_ram_flush_min == VK_WHOLE_SIZE);
        assert(fs->vertex_ram_flush_max == 0);
        /* Finalization is safe after partial initialization and if repeated. */
        pgraph_vk_finalize_vertex_ram_state(fs);
#endif
        free(fs->vertex_ram.mapped);
    }
    free(renderer.cb_vertex_pages);
    free(latest);
}

static void write_bytes(size_t offset, size_t count, uint8_t value,
                        bool keep_reads)
{
    assert(offset + count <= total_bytes);
    uint8_t *data = malloc(count ? count : 1);
    assert(data);
    memset(data, value, count);
    if (!keep_reads) {
        bitmap_clear(renderer.cb_vertex_pages, 0, renderer.bitmap_size);
    }
    pgraph_vk_update_vertex_ram_buffer(&pg, offset, data, count);
    memcpy(latest + offset, data, count);
    free(data);
    verify();
}

static void multi_generation_history(void)
{
    init(3, 8 * TARGET_PAGE_SIZE);
    rotate();
    write_bytes(TARGET_PAGE_SIZE + 17, 3, 0x41, false);
    rotate();
    write_bytes(5 * TARGET_PAGE_SIZE + 23, 7, 0x72, false);
    /* Slot 0 must receive A from slot 1 as well as B from slot 2. */
    rotate();
    rotate();
    rotate();
    cleanup();
}

#ifndef NEGATIVE_CONTROL
static void reset_upload_work(void)
{
    memset(&g_nv2a_stats, 0, sizeof(g_nv2a_stats));
    mirror_bytes = mirror_runs = 0;
    last_copy_size = 0;
}

static void upload_cases(int depth)
{
    init(depth, 6 * TARGET_PAGE_SIZE + 13);
    for (int i = 0; i < depth; i++) {
        rotate();
    }
    bitmap_clear(renderer.cb_vertex_pages, 0, renderer.bitmap_size);
    bitmap_set(renderer.cb_vertex_pages, 0, 1);
    FrameStagingState *fs = &renderer.frame_staging[renderer.current_frame];
    fs->vertex_ram_flush_min = VK_WHOLE_SIZE;
    fs->vertex_ram_flush_max = 0;
    unsigned finishes = finish_calls;
    reset_upload_work();
    /* An identical partial upload must neither finish nor write mapped RAM. */
    pgraph_vk_update_vertex_ram_buffer(&pg, 5, latest + 5, 19);
    if (finish_calls != finishes) {
        fprintf(stderr, "unchanged upload caused finish\n");
        exit(1);
    }
    assert(!mirror_bytes && !mirror_runs);
    assert(fs->vertex_ram_flush_min == VK_WHOLE_SIZE);
    assert(fs->vertex_ram_flush_max == 0);
    assert(bit_set(renderer.cb_vertex_pages, 0));
    for (int i = 0; i < depth; i++) {
        assert(find_next_bit(renderer.frame_staging[i].vertex_ram_stale,
                             renderer.bitmap_size, 0) == renderer.bitmap_size);
    }
#if NV2A_PERF_LOG
    assert(g_nv2a_stats.vram_working.upload_checks == 1);
    assert(g_nv2a_stats.vram_working.upload_check_bytes == 19);
    assert(g_nv2a_stats.vram_working.upload_nops == 1);
    assert(g_nv2a_stats.vram_working.upload_nop_bytes == 19);
    assert(g_nv2a_stats.vram_working.upload_finishes_avoided == 1);
#endif
    reset_upload_work();
    pgraph_vk_update_vertex_ram_buffer(&pg, total_bytes, latest, 0);
    assert(!mirror_bytes && finish_calls == finishes);
    /* All-equal aligned pages, including reservations outside the old read. */
    pgraph_vk_update_vertex_ram_buffer(&pg, 0, latest, 4 * TARGET_PAGE_SIZE);
    assert(!mirror_bytes && finish_calls == finishes);
    assert(find_next_zero_bit(renderer.cb_vertex_pages, 4, 0) == 4);
    verify();

    /* Initialized metadata is insufficient when an uploaded bit is missing. */
    bitmap_clear(renderer.frame_staging[renderer.current_frame].uploaded_bitmap,
                 0, 1);
    reset_upload_work();
    pgraph_vk_update_vertex_ram_buffer(&pg, 5, latest + 5, 19);
    assert(finish_calls == ++finishes && last_copy_size == 19);
    assert(g_nv2a_stats.vram_working.upload_checks == 0);
    verify();
    bitmap_set(renderer.cb_vertex_pages, 0, 1);
    fs = &renderer.frame_staging[renderer.current_frame];
    fs->vertex_ram_initialized = false;
    verify_suspended = true;
    reset_upload_work();
    pgraph_vk_update_vertex_ram_buffer(&pg, 5, latest + 5, 19);
    assert(finish_calls == ++finishes && last_copy_size == 19);
    assert(g_nv2a_stats.vram_working.upload_checks == 0);
    fs->vertex_ram_initialized = true;
    verify_suspended = false;
    verify();
    /* No pending read: even equal data takes the ordinary no-compare path. */
    bitmap_clear(renderer.cb_vertex_pages, 0, renderer.bitmap_size);
    reset_upload_work();
    pgraph_vk_update_vertex_ram_buffer(&pg, 5, latest + 5, 19);
    assert(finish_calls == finishes && mirror_bytes == 19);
    assert(g_nv2a_stats.vram_working.upload_checks == 0);

    uint8_t *source = malloc(total_bytes);
    assert(source);
    memcpy(source, latest, total_bytes);
    /* Only B differs; pending reads of unchanged A must not force finish. */
    source[TARGET_PAGE_SIZE + 7] ^= 0x41;
    bitmap_clear(renderer.cb_vertex_pages, 0, renderer.bitmap_size);
    bitmap_set(renderer.cb_vertex_pages, 0, 1);
    reset_upload_work();
    pgraph_vk_update_vertex_ram_buffer(&pg, 0, source, 4 * TARGET_PAGE_SIZE);
    assert(finish_calls == finishes && mirror_bytes == TARGET_PAGE_SIZE);
    memcpy(latest, source, 4 * TARGET_PAGE_SIZE);
    verify();
#if NV2A_PERF_LOG
    assert(g_nv2a_stats.vram_working.upload_checks == 1);
    assert(g_nv2a_stats.vram_working.upload_nops == 0);
    assert(g_nv2a_stats.vram_working.upload_trim_bytes == 3 * TARGET_PAGE_SIZE);
    assert(g_nv2a_stats.vram_working.upload_finishes_avoided == 1);
#endif
    unsigned long debt_before[NUM_SUBMIT_FRAMES] = { 0 };
    for (int i = 0; i < depth; i++) {
        debt_before[i] = renderer.frame_staging[i].vertex_ram_stale[0];
    }
    fs = &renderer.frame_staging[renderer.current_frame];
    VkDeviceSize flush_min = fs->vertex_ram_flush_min;
    VkDeviceSize flush_max = fs->vertex_ram_flush_max;
    reset_upload_work();
    pgraph_vk_update_vertex_ram_buffer(&pg, TARGET_PAGE_SIZE,
                                      source + TARGET_PAGE_SIZE,
                                      TARGET_PAGE_SIZE);
    assert(finish_calls == finishes && !mirror_bytes);
    assert(fs->vertex_ram_flush_min == flush_min);
    assert(fs->vertex_ram_flush_max == flush_max);
    for (int i = 0; i < depth; i++) {
        assert(renderer.frame_staging[i].vertex_ram_stale[0] == debt_before[i]);
    }
    /* Changed first page: trim the unchanged suffix and its read hazard. */
    source[5] ^= 0x17;
    bitmap_clear(renderer.cb_vertex_pages, 0, renderer.bitmap_size);
    bitmap_set(renderer.cb_vertex_pages, 3, 1);
    reset_upload_work();
    pgraph_vk_update_vertex_ram_buffer(&pg, 0, source, 4 * TARGET_PAGE_SIZE);
    assert(finish_calls == finishes && mirror_bytes == TARGET_PAGE_SIZE);
    memcpy(latest, source, total_bytes);
    verify();
    /* A true overlap on that changed page still finishes and rotates first. */
    source[TARGET_PAGE_SIZE + 7] ^= 0x22;
    bitmap_clear(renderer.cb_vertex_pages, 0, renderer.bitmap_size);
    bitmap_set(renderer.cb_vertex_pages, 1, 1);
    int prior_slot = renderer.current_frame;
    reset_upload_work();
    finish_source = source;
    pgraph_vk_update_vertex_ram_buffer(&pg, 0, source, 4 * TARGET_PAGE_SIZE);
    assert(finish_calls == ++finishes);
    assert(renderer.current_frame == (prior_slot + 1) % depth);
    memcpy(latest, source, 4 * TARGET_PAGE_SIZE);
    verify();
    assert(last_copy_size == 4 * TARGET_PAGE_SIZE);
#if NV2A_PERF_LOG
    assert(g_nv2a_stats.vram_working.upload_finishes_avoided == 0);
    assert(g_nv2a_stats.vram_working.upload_trim_bytes == 0);
#endif
    /* An unaligned changed interval stays intact, retaining its true hazard. */
    source[TARGET_PAGE_SIZE + 9] ^= 0x13;
    bitmap_clear(renderer.cb_vertex_pages, 0, renderer.bitmap_size);
    bitmap_set(renderer.cb_vertex_pages, 1, 1);
    reset_upload_work();
    pgraph_vk_update_vertex_ram_buffer(&pg, TARGET_PAGE_SIZE + 3,
                                      source + TARGET_PAGE_SIZE + 3,
                                      TARGET_PAGE_SIZE + 2);
    assert(finish_calls == ++finishes);
    assert(mirror_bytes >= TARGET_PAGE_SIZE + 2);
    memcpy(latest, source, total_bytes);
    verify();

    /* Download both rotates and changes source bytes before equality checks. */
    memcpy(source, latest, total_bytes);
    download_rotate = true;
    download_source = source;
    prior_slot = renderer.current_frame;
    reset_upload_work();
    pgraph_vk_update_vertex_ram_buffer(&pg, 0, source, 2 * TARGET_PAGE_SIZE);
    memcpy(latest, source, total_bytes);
    assert(renderer.current_frame == (prior_slot + 1) % depth);
    assert(finish_calls == finishes); /* Dirty tail no longer overlaps A. */
    assert(mirror_bytes >= TARGET_PAGE_SIZE);
    verify();
    for (int i = 0; i < 2 * depth; i++) {
        rotate();
    }
    free(source);
    cleanup();
    printf("Vertex upload guards/trim passed at depth %d\n", depth);
}

#ifndef UPLOAD_NEGATIVE_CONTROL
static int compare_u64(const void *a, const void *b)
{
    uint64_t left = *(const uint64_t *)a, right = *(const uint64_t *)b;
    return (left > right) - (left < right);
}

static uint64_t monotonic_ns(void)
{
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return now.tv_sec * UINT64_C(1000000000) + now.tv_nsec;
}

static void benchmark_comparisons(void)
{
    /* Prevent hoisting the comparisons out of the timed loop. */
    void (*volatile compare)(const StorageBuffer *, hwaddr *,
                             const uint8_t **, VkDeviceSize *) =
        trim_unchanged_vertex_ram_pages;
    uint64_t sink = 0;
    puts("Synthetic host helper ns/call; median of 7 samples, 2000 calls each; "
         "excludes driver/fence waits and is not device FPS");
    for (size_t size = 4096; size <= 262144; size *= 4) {
        uint8_t *mapped = calloc(size, 1), *data = calloc(size, 1);
        assert(mapped && data);
        StorageBuffer buffer = { mapped, size, 0 };
        for (int pattern = 0; pattern < 3; pattern++) {
            memset(data, 0, size);
            if (pattern) {
                data[pattern == 1 ? 0 : size - 1] = 1;
            }
            uint64_t samples[7];
            for (unsigned sample = 0; sample < 7; sample++) {
                uint64_t start = monotonic_ns();
                for (unsigned i = 0; i < 2000; i++) {
                    hwaddr offset = 0;
                    VkDeviceSize remaining = size;
                    const uint8_t *source = data;
                    compare(&buffer, &offset, &source, &remaining);
                    sink += offset + remaining;
                }
                samples[sample] = (monotonic_ns() - start) / 2000;
            }
            qsort(samples, 7, sizeof(samples[0]), compare_u64);
            printf("%zu bytes, %s: median %llu ns/call, range %llu..%llu\n",
                   size, pattern == 0 ? "equal" :
                         pattern == 1 ? "first byte differs" :
                                        "last byte differs",
                   (unsigned long long)samples[3],
                   (unsigned long long)samples[0],
                   (unsigned long long)samples[6]);
        }
        free(mapped);
        free(data);
    }
    assert(sink);
}
#endif

static uint32_t random_state = 0x714ce529;
static uint32_t next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void depth_cases(int depth)
{
    init(depth, 70 * TARGET_PAGE_SIZE + 13);
    /* First-copy policy and multi-word bitmaps, including partial last page. */
    for (int i = 0; i < depth; i++) {
        rotate();
    }
    uint64_t bytes = mirror_bytes;
    uint64_t runs = mirror_runs;
    assert(bytes == (uint64_t)(depth - 1) * total_bytes);
    for (int i = 0; i < 2 * depth; i++) {
        rotate();
    }
    assert(mirror_bytes == bytes && mirror_runs == runs);
    /* Overlapping same-page writes and cross-page ranges accrue all history. */
    write_bytes(5, 19, 0x11, false);
    write_bytes(9, 3, 0x22, false);
    write_bytes(TARGET_PAGE_SIZE - 2, 6, 0x33, false);
    write_bytes(total_bytes - 5, 5, 0x44, false);
    write_bytes(total_bytes, 0, 0, false);
    for (int i = 0; i < 3 * depth; i++) {
        rotate();
    }
    /* Leave destinations untouched over disjoint writes and several wraps. */
    for (unsigned step = 0; step < 600; step++) {
        size_t offset = next_random() % total_bytes;
        size_t requested = 1 + next_random() % 8193;
        size_t count = MIN(requested, total_bytes - offset);
        write_bytes(offset, count, (uint8_t)step, false);
        if (step % 3) {
            rotate();
        }
    }
    /* Real writer: finish before overwrite, then look up the current slot. */
    write_bytes(51, 7, 0xb1, false);
    unsigned previous_finish = finish_calls;
    int previous_slot = renderer.current_frame;
    write_bytes(52, 2, 0xb2, true);
    assert(finish_calls == previous_finish + 1);
    assert(renderer.current_frame == (previous_slot + 1) % depth);
    /* Dormant worker writer is tested as serialized; no new producer exists. */
    RenderCommand command = { .vertex_ram = {
        .offset = total_bytes - 13, .size = 13, .data = malloc(13),
    } };
    assert(command.vertex_ram.data);
    memset(command.vertex_ram.data, 0xc3, 13);
    process_vertex_ram_update(&renderer, &command);
    memset(latest + total_bytes - 13, 0xc3, 13);
    verify();
    /* Full flush clears other slots' uploaded state but retains stale debt. */
    write_bytes(3 * TARGET_PAGE_SIZE, 4, 0xd4, false);
    unsigned long before[NUM_SUBMIT_FRAMES][BITS_TO_LONGS(71)] = { 0 };
    for (int i = 0; i < depth; i++) {
        memcpy(before[i], renderer.frame_staging[i].vertex_ram_stale,
               BITS_TO_LONGS(renderer.bitmap_size) * sizeof(unsigned long));
        renderer.frame_submitted[i] = true;
        renderer.frame_enqueued[i] = true;
        renderer.deferred_framebuffer_count[i] = 1;
        renderer.deferred_framebuffers[i][0] = 42;
    }
    unsigned old_waits = flush_waits, old_idle = idle_waits;
    pgraph_vk_flush_all_frames(&pg);
    assert(flush_waits == old_waits + depth && idle_waits == old_idle + 1);
    for (int i = 0; i < depth; i++) {
        assert(!renderer.frame_submitted[i] && !renderer.frame_enqueued[i]);
        assert(!memcmp(before[i], renderer.frame_staging[i].vertex_ram_stale,
                       BITS_TO_LONGS(renderer.bitmap_size) * sizeof(long)));
    }
    write_bytes(7 * TARGET_PAGE_SIZE + 3, 11, 0xe5, false);
    for (int i = 0; i < 2 * depth; i++) {
        rotate();
    }
    write_bytes(0, total_bytes, 0x69, false);
    for (int i = 0; i < 2 * depth; i++) {
        rotate();
    }
    /* A live depth request cannot expose an unallocated slot. */
    xemu_set_submit_frames(depth == 3 ? 1 : 3);
    assert(renderer.num_active_frames == depth);
    for (int i = 0; i < 2 * depth; i++) {
        rotate();
    }
#if NV2A_PERF_LOG
    assert(g_nv2a_stats.vram_working.first_copy_bytes ==
           (uint64_t)(depth - 1) * total_bytes);
#else
    assert(clock_calls == 0);
    VertexRamTimingWork zero = {0};
    assert(!memcmp(&zero, &g_nv2a_stats.vram_working, sizeof(zero)));
#endif
    cleanup();
}

static void allocation_and_depth_policy(void)
{
    memset(&renderer, 0, sizeof(renderer));
    renderer.bitmap_size = 71;
    for (int failure = 1; failure <= 2; failure++) {
        FrameStagingState fs = {0};
        Error *error = NULL;
        allocation_call = 0;
        fail_allocation = failure;
        assert(!pgraph_vk_init_vertex_ram_state(&renderer, &fs, &error));
        assert(error && !fs.uploaded_bitmap && !fs.vertex_ram_stale);
        assert(!fs.vertex_ram_initialized);
        pgraph_vk_finalize_vertex_ram_state(&fs);
    }
    fail_allocation = 0;
    /* Both the actual setter and init capture clamp incoming requests. */
    xemu_set_submit_frames(0);
    assert(xemu_get_submit_frames() == 1);
    xemu_set_submit_frames(4);
    assert(xemu_get_submit_frames() == 3);
    for (int requested = -1; requested <= 4; requested++) {
        g_xemu_submit_frames = requested;
        capture_frame_depth(&renderer);
        assert(renderer.num_active_frames == MAX(1, MIN(requested, 3)));
        assert(renderer.current_frame == 0);
    }
    init(1, TARGET_PAGE_SIZE + 1);
    xemu_set_submit_frames(3);
    assert(renderer.num_active_frames == 1);
    rotate();
    cleanup();
    /* A fresh initialization consumes the already pending request. */
    capture_frame_depth(&renderer);
    assert(renderer.num_active_frames == 3);
    init(3, TARGET_PAGE_SIZE + 1);
    rotate();
    rotate();
    cleanup();
}

static void deterministic_sparse_reduction(void)
{
    init(2, 16 * TARGET_PAGE_SIZE);
    rotate();
    rotate();
    write_bytes(3, 4, 0x88, false);
    write_bytes(total_bytes - 7, 4, 0x99, false);
    mirror_bytes = mirror_runs = clock_calls = 0;
    memset(&g_nv2a_stats, 0, sizeof(g_nv2a_stats));
    rotate();
    assert(mirror_bytes == 2 * TARGET_PAGE_SIZE && mirror_runs == 2);
    assert(renderer.frame_staging[renderer.current_frame].vertex_ram_flush_min
           == 0);
    assert(renderer.frame_staging[renderer.current_frame].vertex_ram_flush_max
           == total_bytes);
#if NV2A_PERF_LOG
    assert(clock_calls == 4);
    assert(g_nv2a_stats.vram_working.span_copy_bytes == mirror_bytes);
    assert(g_nv2a_stats.vram_working.span_copy_runs == mirror_runs);
    assert(g_nv2a_stats.vram_working.span_copy_ns == 200);
#else
    assert(clock_calls == 0);
#endif
    size_t old_min_max_bytes = total_bytes - 6;
    printf("Sparse byte reduction: %llu bytes/2 runs versus %zu-byte old "
           "min/max span; identical byte oracle\n",
           (unsigned long long)mirror_bytes, old_min_max_bytes);
    cleanup();
}
#endif

int main(int argc, char **argv)
{
#ifndef NEGATIVE_CONTROL
    if (argc > 1 && !strcmp(argv[1], "bounds")) {
        init(1, TARGET_PAGE_SIZE);
        pgraph_vk_update_vertex_ram_buffer(&pg, total_bytes + 1, latest, 0);
        return 0;
    }
#ifndef UPLOAD_NEGATIVE_CONTROL
    if (argc > 1 && !strcmp(argv[1], "benchmark")) {
        benchmark_comparisons();
        return 0;
    }
#endif
    for (int depth = 1; depth <= 3; depth++) {
        upload_cases(depth);
    }
#else
    (void)argc;
    (void)argv;
#endif
    multi_generation_history();
#ifndef NEGATIVE_CONTROL
    for (int depth = 1; depth <= 3; depth++) {
        depth_cases(depth);
    }
    allocation_and_depth_policy();
    deterministic_sparse_reduction();
    printf("Vertex propagation passed (NV2A_PERF_LOG=%d): depths 1/2/3, "
           "1800 seeded writes, history, tail, flush, worker, recreation, "
           "depth capture; PERF=0 makes zero clock calls\n", NV2A_PERF_LOG);
#endif
    return 0;
}
