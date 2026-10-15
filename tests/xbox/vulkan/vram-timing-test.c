/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real rotation memcpy/wait blocks and profiling code, without Vulkan. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define TARGET_PAGE_SIZE 8
#define DIV_ROUND_UP(a, b) (((a) + (b) - 1) / (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define BITS_TO_LONGS(bits) (((bits) + 8 * sizeof(long) - 1) / \
                           (8 * sizeof(long)))
#define VK_WHOLE_SIZE UINT64_MAX
#define VK_TRUE 1
#define VK_CHECK(result) assert((result) == 0)
#define qatomic_read(ptr) (*(ptr))

#include "vram-timing-types.inc"

static struct {
    VertexRamTimingWork vram_working;
    VertexRamTimingStats vram;
} g_nv2a_stats;

typedef struct StorageBuffer {
    uint8_t *mapped;
    size_t buffer_size;
} StorageBuffer;
typedef struct FrameStagingState {
    StorageBuffer vertex_ram;
    uint64_t vertex_ram_flush_min, vertex_ram_flush_max;
    unsigned long vertex_ram_stale[1];
    bool vertex_ram_initialized;
    unsigned long uploaded_bitmap[1];
} FrameStagingState;
typedef struct PGRAPHVkState {
    FrameStagingState frame_staging[3];
    bool frame_submitted[3];
    int frame_fences[3];
    int device;
    int current_frame;
    int num_active_frames;
    size_t bitmap_size;
} PGRAPHVkState;

static size_t find_next_bit(const unsigned long *map, size_t end,
                            size_t first)
{
    while (first < end && !(*map & (1UL << first))) {
        first++;
    }
    return first;
}

static size_t find_next_zero_bit(const unsigned long *map, size_t end,
                                 size_t first)
{
    while (first < end && (*map & (1UL << first))) {
        first++;
    }
    return first;
}

static void bitmap_clear(unsigned long *map, size_t first, size_t count)
{
    while (count--) {
        *map &= ~(1UL << first++);
    }
}

static int64_t samples[2];
static unsigned clock_calls, wait_calls, data_copies, bitmap_copies;
#if NV2A_PERF_LOG
static int64_t nv2a_clock_ns(void)
{
    assert(clock_calls < 2);
    return samples[clock_calls++];
}
#endif

static int vkWaitForFences(int device, unsigned count, const int *fences,
                          unsigned wait_all, uint64_t timeout)
{
    (void)device;
    assert(count == 1 && *fences == 11);
    assert(wait_all == VK_TRUE && timeout == UINT64_MAX);
    assert(clock_calls == NV2A_PERF_LOG);
    wait_calls++;
    return 0;
}

static uint8_t source_data[64], destination_data[64];
static PGRAPHVkState renderer;

static void *checked_memcpy(void *dst, const void *src, size_t size)
{
    if (dst == renderer.frame_staging[1].uploaded_bitmap) {
        /* The bitmap clone is intentionally outside the data-copy timer. */
        assert(clock_calls == 0);
        bitmap_copies++;
    } else {
        assert(clock_calls == NV2A_PERF_LOG);
        data_copies++;
    }
    return memcpy(dst, src, size);
}

#define memcpy checked_memcpy
#include "vram-timing-functions.inc"
#undef memcpy
#include "vram-timing-profile.inc"

static void reset(int64_t first, int64_t last)
{
    memset(&renderer, 0, sizeof(renderer));
    memset(&g_nv2a_stats, 0, sizeof(g_nv2a_stats));
    for (unsigned i = 0; i < sizeof(source_data); i++) {
        source_data[i] = i;
    }
    memset(destination_data, 0xcc, sizeof(destination_data));
    renderer.bitmap_size = 8;
    renderer.num_active_frames = 2;
    renderer.frame_fences[1] = 11;
    renderer.frame_staging[0].vertex_ram =
        (StorageBuffer){ source_data, sizeof(source_data) };
    renderer.frame_staging[1].vertex_ram =
        (StorageBuffer){ destination_data, sizeof(destination_data) };
    renderer.frame_staging[0].uploaded_bitmap[0] = 0x35;
    samples[0] = first;
    samples[1] = last;
    clock_calls = wait_calls = data_copies = bitmap_copies = 0;
}

static void assert_zero_work(void)
{
    VertexRamTimingWork zero = {0};
    assert(!memcmp(&zero, &g_nv2a_stats.vram_working, sizeof(zero)));
}

static void wait_measurement(void)
{
    reset(100, 400);
    exercise_wait(&renderer, 1);
    assert(wait_calls == 0 && clock_calls == 0);
    assert_zero_work();
    renderer.frame_submitted[1] = true;
    exercise_wait(&renderer, 1);
    assert(wait_calls == 1 && clock_calls == 2 * NV2A_PERF_LOG);
    assert(renderer.frame_submitted[1]);
#if NV2A_PERF_LOG
    assert(g_nv2a_stats.vram_working.next_wait_calls == 1);
    assert(g_nv2a_stats.vram_working.next_wait_ns == 300);
    assert(g_nv2a_stats.vram_working.first_copy_bytes == 0);
#else
    assert_zero_work();
#endif
}

static void copy_measurement(bool first, bool changed, bool backwards_clock)
{
    reset(100, backwards_clock ? 50 : 450);
    FrameStagingState *cur = &renderer.frame_staging[0];
    FrameStagingState *next = &renderer.frame_staging[1];
    next->vertex_ram_initialized = !first;
    if (changed) {
        next->vertex_ram_stale[0] = 6; /* Adjacent pages 1 and 2. */
    }
    pgraph_vk_propagate_vertex_ram(&renderer, 1);
    bool copied = first || changed;
    assert(bitmap_copies == 1 && data_copies == copied);
    assert(clock_calls == 2 * copied * NV2A_PERF_LOG);
    assert(next->uploaded_bitmap[0] == cur->uploaded_bitmap[0]);
    assert(next->vertex_ram_stale[0] == 0);
    for (unsigned i = 0; i < sizeof(source_data); i++) {
        bool in_copy = first || (changed && i >= 8 && i < 24);
        assert(destination_data[i] == (in_copy ? source_data[i] : 0xcc));
    }
    assert(next->vertex_ram_flush_min ==
           (first ? 0 : changed ? 8 : VK_WHOLE_SIZE));
    assert(next->vertex_ram_flush_max == (first ? 64 : changed ? 24 : 0));
#if NV2A_PERF_LOG
    VertexRamTimingWork *w = &g_nv2a_stats.vram_working;
    int64_t elapsed = backwards_clock || !copied ? 0 : 350;
    assert(w->rotations == 1 && w->next_wait_calls == 0);
    assert(w->first_copy_runs == first);
    assert(w->span_copy_runs == (!first && changed));
    assert(w->first_copy_bytes == (first ? 64 : 0));
    assert(w->span_copy_bytes == (!first && changed ? 16 : 0));
    assert(w->first_copy_ns == (first ? elapsed : 0));
    assert(w->span_copy_ns == (!first ? elapsed : 0));
#else
    assert_zero_work();
#endif
}

static void per_flip_snapshot(void)
{
    reset(0, 0);
#if NV2A_PERF_LOG
    g_nv2a_stats.vram_working = (VertexRamTimingWork){
        .rotations = 5, .next_wait_calls = 2,
        .next_wait_ns = 7000000,
        .first_copy_ns = 3000000, .span_copy_ns = 1000000,
        .first_copy_runs = 1, .span_copy_runs = 4,
        .first_copy_bytes = 4 * 1048576, .span_copy_bytes = 2 * 1048576,
        .upload_checks = 5, .upload_check_bytes = 2 * 1048576,
        .upload_nops = 2, .upload_nop_bytes = 1048576,
        .upload_trim_bytes = 524288, .upload_finishes_avoided = 3,
    };
#endif
    snapshot_vram_timing();
    assert_zero_work();
#if NV2A_PERF_LOG
    VertexRamTimingStats *p = &g_nv2a_stats.vram;
    assert(fabsf(p->rotations - 1.0f) < 0.00001f);
    assert(fabsf(p->next_wait_ms - 1.4f) < 0.00001f);
    assert(fabsf(p->first_copy_ms + p->span_copy_ms - 0.8f) < 0.00001f);
    assert(fabsf(p->next_wait_calls - 0.4f) < 0.00001f);
    assert(fabsf(p->first_copy_mib + p->span_copy_mib - 1.2f) < 0.00001f);
    assert(fabsf(p->first_copy_runs + p->span_copy_runs - 1.0f) < 0.00001f);
    assert(fabsf(p->upload_checks - 1.0f) < 0.00001f);
    assert(fabsf(p->upload_check_mib - 0.4f) < 0.00001f);
    assert(fabsf(p->upload_nops - 0.4f) < 0.00001f);
    assert(fabsf(p->upload_nop_mib - 0.2f) < 0.00001f);
    assert(fabsf(p->upload_trim_mib - 0.1f) < 0.00001f);
    assert(fabsf(p->upload_finishes_avoided - 0.6f) < 0.00001f);
    snapshot_vram_timing();
    assert_zero_work();
    assert(fabsf(p->next_wait_ms - 1.12f) < 0.00001f);
    assert(fabsf(p->first_copy_mib + p->span_copy_mib - 0.96f) < 0.00001f);
    assert(fabsf(p->upload_checks - 0.8f) < 0.00001f);
    assert(fabsf(p->upload_finishes_avoided - 0.48f) < 0.00001f);
    char output[512];
    nv2a_profile_get_vram_timing_str(output, sizeof(output));
    printf("Synthetic xemu-vram example: %s\n", output);
#endif
}

int main(void)
{
    wait_measurement();
    reset(100, 50);
    renderer.frame_submitted[1] = true;
    exercise_wait(&renderer, 1);
    assert(g_nv2a_stats.vram_working.next_wait_ns == 0);
    copy_measurement(true, false, false);
    copy_measurement(false, true, false);
    copy_measurement(false, false, false);
    copy_measurement(true, false, true);
    copy_measurement(false, true, true);
    per_flip_snapshot();
    printf("VRAM telemetry accounting passed (NV2A_PERF_LOG=%d); "
           "disabled measurements make zero clock calls\n", NV2A_PERF_LOG);
    return 0;
}
