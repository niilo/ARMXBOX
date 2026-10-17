/*
 * Vulkan draw guard equivalence test.
 *
 * Compiles against the real renderer types and pgraph_vk_snapshot_state().
 * The four-field capture is an independent reference for the draw assertion
 * inputs; this does not execute draws or validate Vulkan synchronization.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hw/xbox/nv2a/pgraph/vk/renderer.h"
#include <time.h>

typedef struct DrawGuard {
    uint32_t primitive_mode;
    bool clearing;
    uint32_t control_0;
    uint32_t setupraster;
} DrawGuard;

static DrawGuard capture_guard(PGRAPHState *pg)
{
    return (DrawGuard) {
        .primitive_mode = pg->primitive_mode,
        .clearing = pg->clearing,
        .control_0 = pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0),
        .setupraster = pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER),
    };
}

static unsigned snapshot_checks(const RenderCommandSnapshot *snap,
                                PGRAPHState *pg)
{
    return (snap->primitive_mode == pg->primitive_mode) |
           ((snap->clearing == pg->clearing) << 1) |
           ((snapshot_reg_r(snap, NV_PGRAPH_CONTROL_0) ==
             pgraph_vk_reg_r(pg, NV_PGRAPH_CONTROL_0)) << 2) |
           ((snapshot_reg_r(snap, NV_PGRAPH_SETUPRASTER) ==
             pgraph_vk_reg_r(pg, NV_PGRAPH_SETUPRASTER)) << 3);
}

static unsigned guard_checks(const DrawGuard *guard, PGRAPHState *pg)
{
    return (guard->primitive_mode == pg->primitive_mode) |
           ((guard->clearing == pg->clearing) << 1) |
           ((guard->control_0 ==
             pgraph_vk_reg_r(pg, NV_PGRAPH_CONTROL_0)) << 2) |
           ((guard->setupraster ==
             pgraph_vk_reg_r(pg, NV_PGRAPH_SETUPRASTER)) << 3);
}

static uint64_t checksum(DrawGuard g)
{
    return (uint64_t)g.primitive_mode + ((uint64_t)g.clearing << 32) +
           ((uint64_t)g.control_0 << 1) + ((uint64_t)g.setupraster << 2);
}

static __attribute__((noinline)) uint64_t baseline(PGRAPHState *pg)
{
    RenderCommandSnapshot snap;
    pgraph_vk_snapshot_state(pg, &snap);
    return checksum((DrawGuard) {
        .primitive_mode = snap.primitive_mode,
        .clearing = snap.clearing,
        .control_0 = snapshot_reg_r(&snap, NV_PGRAPH_CONTROL_0),
        .setupraster = snapshot_reg_r(&snap, NV_PGRAPH_SETUPRASTER),
    });
}

static __attribute__((noinline)) uint64_t candidate(PGRAPHState *pg)
{
    return checksum(capture_guard(pg));
}

static uint64_t nanos(void)
{
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

int main(int argc, char **argv)
{
    PGRAPHState *pg = g_new0(PGRAPHState, 1);
    PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    RenderCommandSnapshot *snap = g_new0(RenderCommandSnapshot, 1);
    RenderCommandSnapshot *active = g_new0(RenderCommandSnapshot, 1);
    pg->vk_renderer_state = r;
    unsigned checks = 0;

    for (unsigned seed = 0; seed < 512; seed++) {
        pg->primitive_mode = seed * 0x9e3779b9u;
        pg->clearing = seed & 1;
        pg->regs_[NV_PGRAPH_CONTROL_0] = ~pg->primitive_mode;
        pg->regs_[NV_PGRAPH_SETUPRASTER] = seed * 0x1234567u;
        pgraph_vk_snapshot_state(pg, snap);
        DrawGuard guard = capture_guard(pg);
        assert(snapshot_checks(snap, pg) == 15);
        assert(guard_checks(&guard, pg) == 15);
        assert(baseline(pg) == candidate(pg));

        for (unsigned mutations = 0; mutations < 16; mutations++) {
            pg->primitive_mode ^= !!(mutations & 1);
            pg->clearing ^= !!(mutations & 2);
            pg->regs_[NV_PGRAPH_CONTROL_0] ^= !!(mutations & 4);
            pg->regs_[NV_PGRAPH_SETUPRASTER] ^= !!(mutations & 8);
            assert(snapshot_checks(snap, pg) == (15 ^ mutations));
            assert(snapshot_checks(snap, pg) == guard_checks(&guard, pg));
            checks++;

            /*
             * The old capture reads raw registers even with active_snap set.
             * Its end checks use snapshot-aware reads. Preserve that detail.
             */
            *active = *snap;
            active->regs[NV_PGRAPH_CONTROL_0] ^= seed;
            active->regs[NV_PGRAPH_SETUPRASTER] ^= ~seed;
            r->active_snap = active;
            assert(snapshot_checks(snap, pg) == guard_checks(&guard, pg));
            checks++;
            r->active_snap = NULL;

            pg->primitive_mode ^= !!(mutations & 1);
            pg->clearing ^= !!(mutations & 2);
            pg->regs_[NV_PGRAPH_CONTROL_0] ^= !!(mutations & 4);
            pg->regs_[NV_PGRAPH_SETUPRASTER] ^= !!(mutations & 8);
        }
    }
    printf("snapshot_size=%zu guard_size=%zu semantic_checks=%u\n",
           sizeof(*snap), sizeof(DrawGuard), checks);

    if (argc == 1) {
        g_free(active);
        g_free(snap);
        g_free(r);
        g_free(pg);
        return 0;
    }
    assert(argc == 2 && !strcmp(argv[1], "--benchmark"));

    const unsigned iterations = 200000;
    /* Volatile keeps the optional benchmark's result observable. */
    volatile uint64_t sink = 0;
    for (unsigned run = 0; run < 11; run++) {
        for (unsigned phase = 0; phase < 2; phase++) {
            bool narrow = (run + phase) & 1;
            uint64_t start = nanos();
            for (unsigned i = 0; i < iterations; i++) {
                pg->primitive_mode += 17;
                sink += narrow ? candidate(pg) : baseline(pg);
            }
            uint64_t elapsed = nanos() - start;
            printf("run=%u variant=%s ns_per_capture=%.3f\n", run,
                   narrow ? "candidate" : "baseline",
                   (double)elapsed / iterations);
        }
    }
    printf("checksum=%" PRIu64 "\n", sink);
    g_free(active);
    g_free(snap);
    g_free(r);
    g_free(pg);
    return 0;
}
