/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Actual old/new handlers and expansion/reset functions; no GPU or games. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* Smaller odd capacity exercises both packed boundaries quickly. */
#define NV2A_MAX_BATCH_LENGTH 4095
#define PRIM_TYPE_INVALID 0
#define NV_KELVIN_PRIMITIVE 0x97
#define NV2A_DPRINTF(...) do { } while (0)
#define METHOD_HANDLER_ARG_DECL \
    NV2AState *d, PGRAPHState *pg, unsigned int subchannel, \
    unsigned int method, \
    uint32_t parameter, uint32_t *parameters, size_t num_words_available, \
    size_t *num_words_consumed, bool inc
#include "index-types.inc"
static struct {
    CpuTimingWork cpu_working;
    CpuTimingStats cpu;
} g_nv2a_stats;

typedef struct NV2AState NV2AState;
typedef struct Renderer {
    struct { void (*flush_draw)(NV2AState *); } ops;
} Renderer;
typedef struct PGRAPHState {
    Renderer *renderer;
    unsigned int primitive_mode;
    unsigned int inline_elements_length;
    uint32_t inline_elements[NV2A_MAX_BATCH_LENGTH];
    uint32_t canary;
    unsigned int inline_array_length, inline_buffer_length;
    unsigned int draw_arrays_length, draw_arrays_start[4], draw_arrays_count[4];
    unsigned int draw_arrays_min_start, draw_arrays_max_count;
    bool draw_arrays_prevent_connect;
} PGRAPHState;
struct NV2AState { PGRAPHState pgraph; };
typedef void (*MethodFunc)(METHOD_HANDLER_ARG_DECL);

static uint32_t ldl_le_p(const void *pointer)
{
    const uint8_t *p = pointer;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static unsigned flushes, checks, expands;
static uint64_t flushed_hash;
static void flush_draw(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    flushes++;
    flushed_hash += pg->inline_elements_length;
    for (unsigned i = 0; i < pg->inline_elements_length; i++) {
        flushed_hash = flushed_hash * 31 + pg->inline_elements[i];
    }
}
static Renderer renderer = { { flush_draw } };

#include "index-shared.inc"

static void counted_check(PGRAPHState *pg)
{
#if INDEX_INSTRUMENT
    checks++;
#endif
    pgraph_check_within_begin_end_block(pg);
}

static void counted_expand(NV2AState *d)
{
#if INDEX_INSTRUMENT
    expands++;
#endif
    pgraph_expand_draw_arrays(d);
}

static uint32_t trace_words[4096];
static unsigned trace_length;
#if TRACE_NV2A_PGRAPH_METHOD_ENABLED
static void pgraph_method_log(unsigned subchannel, unsigned graphics_class,
                              unsigned method, uint32_t parameter)
{
    assert(subchannel == 3 && graphics_class == NV_KELVIN_PRIMITIVE);
    assert(method == 0x1800 || method == 0x1808);
    assert(trace_length < 4096);
    trace_words[trace_length++] = parameter;
}
#endif

#define pgraph_check_within_begin_end_block counted_check
#define pgraph_expand_draw_arrays counted_expand
#define DEF_METHOD(gclass, name) \
    static void candidate_##name(METHOD_HANDLER_ARG_DECL)
#include "index-candidate.inc"
#undef DEF_METHOD
#define DEF_METHOD_NON_INC(gclass, name) \
    static void original_##name##_inner(METHOD_HANDLER_ARG_DECL); \
    static void original_##name(METHOD_HANDLER_ARG_DECL) \
    { pgraph_method_non_inc(original_##name##_inner, METHOD_HANDLER_ARGS); } \
    static void original_##name##_inner(METHOD_HANDLER_ARG_DECL)
#include "index-original.inc"
#undef DEF_METHOD_NON_INC
#undef pgraph_check_within_begin_end_block
#undef pgraph_expand_draw_arrays
#include "index-profile.inc"

static uint32_t random_state = 0xa7c331f5;
static uint32_t random32(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void seed(NV2AState *d)
{
    memset(d, 0, sizeof(*d));
    d->pgraph.renderer = &renderer;
    d->pgraph.primitive_mode = 1;
    d->pgraph.canary = 0xe571a903;
    d->pgraph.inline_elements_length = random32() % 32;
    for (unsigned i = 0; i < d->pgraph.inline_elements_length; i++) {
        d->pgraph.inline_elements[i] = random32();
    }
    d->pgraph.inline_array_length = 7;
    d->pgraph.inline_buffer_length = 11;
    d->pgraph.draw_arrays_length = random32() % 4;
    d->pgraph.draw_arrays_min_start = 29;
    d->pgraph.draw_arrays_max_count = 107;
    d->pgraph.draw_arrays_prevent_connect = true;
    for (unsigned i = 0; i < 4; i++) {
        d->pgraph.draw_arrays_start[i] = random32() % 65536;
        d->pgraph.draw_arrays_count[i] = random32() % 64;
    }
}

typedef struct Observation {
    unsigned flush_count, check_count, expand_count, trace_count;
    uint64_t flush_hash;
    uint32_t trace[4096];
    size_t consumed;
} Observation;

static Observation execute(MethodFunc fn, NV2AState *d, bool packed,
                           bool inc, uint32_t parameter, uint32_t *words,
                           size_t count)
{
    flushes = checks = expands = trace_length = 0;
    flushed_hash = 0;
    size_t consumed = 1;
    memset(&g_nv2a_stats.cpu_working, 0, sizeof(g_nv2a_stats.cpu_working));
    fn(d, &d->pgraph, 3, packed ? 0x1800 : 0x1808, parameter, words,
       count, &consumed, inc);
    Observation result = { flushes, checks, expands, trace_length,
                           flushed_hash, { 0 }, consumed };
    memcpy(result.trace, trace_words, trace_length * sizeof(uint32_t));
    return result;
}

static void check_case(NV2AState *initial, bool packed, bool inc,
                       uint32_t parameter, uint32_t *words, size_t count)
{
    NV2AState reference = *initial, candidate = *initial;
    Observation old = execute(packed ? original_ARRAY_ELEMENT16 :
                                       original_ARRAY_ELEMENT32,
                              &reference, packed, inc, parameter, words, count);
    Observation now = execute(packed ? candidate_ARRAY_ELEMENT16 :
                                       candidate_ARRAY_ELEMENT32,
                              &candidate, packed, inc, parameter, words, count);
    assert(!memcmp(&reference, &candidate, sizeof(reference)));
    assert(candidate.pgraph.canary == 0xe571a903);
    assert(old.consumed == now.consumed);
    assert(now.consumed == (inc ? 1 : count));
    assert(old.flush_count == now.flush_count &&
           old.flush_hash == now.flush_hash);
    assert(old.expand_count == now.expand_count);
    assert(old.trace_count == now.trace_count);
    assert(!memcmp(old.trace, now.trace, now.trace_count * sizeof(uint32_t)));
#if INDEX_INSTRUMENT
    assert(now.check_count == 1 && old.check_count == (inc ? 1 : count));
#endif
#if TRACE_NV2A_PGRAPH_METHOD_ENABLED
    assert(now.trace_count == (inc ? 0 : count - 1));
#endif
    unsigned n = candidate.pgraph.inline_elements_length;
    if (packed) {
        uint32_t word = inc ? parameter : ldl_le_p(words + count - 1);
        assert(candidate.pgraph.inline_elements[n - 2] == (word & 0xffff));
        assert(candidate.pgraph.inline_elements[n - 1] == word >> 16);
    } else {
        assert(candidate.pgraph.inline_elements[n - 1] ==
               (inc ? parameter : ldl_le_p(words + count - 1)));
    }
#if NV2A_PERF_LOG
    CpuTimingWork *w = &g_nv2a_stats.cpu_working;
    assert(w->array16_batches == (!inc && packed));
    assert(w->array32_batches == (!inc && !packed));
    assert(w->array16_words == (!inc && packed ? count : 0));
    assert(w->array32_words == (!inc && !packed ? count : 0));
#else
    CpuTimingWork zero = {0};
    assert(!memcmp(&zero, &g_nv2a_stats.cpu_working, sizeof(zero)));
#endif
}

static void regressions(void)
{
    NV2AState d;
    uint32_t words[2047];
    for (unsigned i = 0; i < 2047; i++) {
        uint8_t *p = (uint8_t *)&words[i];
        uint32_t value = random32();
        for (unsigned b = 0; b < 4; b++) {
            p[b] = value >> (8 * b);
        }
    }
    for (unsigned i = 0; i < 4000; i++) {
        seed(&d);
        bool packed = i & 1, inc = i & 2;
        check_case(&d, packed, inc, random32(), words, 1 + random32() % 128);
    }
    for (unsigned packed = 0; packed <= 1; packed++) {
        for (unsigned inc = 0; inc <= 1; inc++) {
            seed(&d);
            d.pgraph.draw_arrays_length = 0;
            d.pgraph.inline_elements_length = NV2A_MAX_BATCH_LENGTH -
                                              (packed ? 2 : 1);
            check_case(&d, packed, inc, 0x71af0265, words, 1);
            seed(&d);
            d.pgraph.draw_arrays_length = 0;
            d.pgraph.inline_elements_length = 0;
            check_case(&d, packed, inc, 0x71af0265, words, 2047);
            if (inc) {
                /* INC owns parameter even if no array input is accessible. */
                check_case(&d, packed, true, 0x15a702ed, NULL, 0);
            }
            /* Flush/reset must free capacity before computing batch fit. */
            seed(&d);
            d.pgraph.inline_elements_length = NV2A_MAX_BATCH_LENGTH - 2;
            d.pgraph.draw_arrays_length = 2;
            d.pgraph.draw_arrays_count[1] = 3;
            check_case(&d, packed, inc, 0x71af0265, words, 17);
        }
    }
    memset(&g_nv2a_stats, 0, sizeof(g_nv2a_stats));
    g_nv2a_stats.cpu_working.array16_batches = 5;
    g_nv2a_stats.cpu_working.array16_words = 160;
    g_nv2a_stats.cpu_working.array32_batches = 10;
    g_nv2a_stats.cpu_working.array32_words = 640;
    snapshot_cpu_timing();
    assert(fabsf(g_nv2a_stats.cpu.array16_batches - 1) < 0.00001f);
    assert(fabsf(g_nv2a_stats.cpu.array16_words - 32) < 0.00001f);
    assert(fabsf(g_nv2a_stats.cpu.array32_batches - 2) < 0.00001f);
    assert(fabsf(g_nv2a_stats.cpu.array32_words - 128) < 0.00001f);
    assert(!g_nv2a_stats.cpu_working.array16_batches);
    assert(!g_nv2a_stats.cpu_working.array32_words);
    snapshot_cpu_timing();
    assert(fabsf(g_nv2a_stats.cpu.array16_words - 25.6f) < 0.00001f);
    char text[512];
    nv2a_profile_get_cpu_timing_str(text, sizeof(text));
    assert(strstr(text, "Idx16:"));
    printf("Index batches: 4000 randomized + 14 boundary/flush/NULL cases "
           "passed "
           "(trace=%d perf=%d instrument=%d); exact state/LE/consumption/trace\n",
           TRACE_NV2A_PGRAPH_METHOD_ENABLED, NV2A_PERF_LOG, INDEX_INSTRUMENT);
}

static int invalid(const char *mode)
{
    NV2AState d;
    seed(&d);
    d.pgraph.draw_arrays_length = 0;
    uint32_t word = 0x73412506;
    size_t count = 1;
    bool packed = strstr(mode, "16") != NULL;
    if (strstr(mode, "empty")) {
        count = 0;
    } else if (strstr(mode, "full")) {
        d.pgraph.inline_elements_length = NV2A_MAX_BATCH_LENGTH;
    } else {
        d.pgraph.inline_elements_length = NV2A_MAX_BATCH_LENGTH - 1;
    }
    if (strstr(mode, "expand")) {
        d.pgraph.inline_elements_length = NV2A_MAX_BATCH_LENGTH - 3;
        d.pgraph.draw_arrays_length = 1;
        d.pgraph.draw_arrays_count[0] = 2;
    }
    MethodFunc fn = packed ? candidate_ARRAY_ELEMENT16 :
                             candidate_ARRAY_ELEMENT32;
    if (strstr(mode, "original")) {
        fn = original_ARRAY_ELEMENT16;
    }
    execute(fn, &d, packed, false, word, &word, count);
    if (d.pgraph.canary != 0xe571a903) {
        fprintf(stderr, "original packed append overwrote canary\n");
        return 1;
    }
    return 0;
}

static uint64_t monotonic_ns(void)
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

static int compare_u64(const void *a, const void *b)
{
    uint64_t left = *(const uint64_t *)a, right = *(const uint64_t *)b;
    return (left > right) - (left < right);
}

static void benchmark(void)
{
    assert(!INDEX_INSTRUMENT && !NV2A_PERF_LOG &&
           !TRACE_NV2A_PGRAPH_METHOD_ENABLED);
    NV2AState d;
    uint32_t words[2047] = {0};
    const unsigned sizes[] = { 1, 8, 32, 128, 512, 2047 };
    puts("Synthetic index ingestion ns/batch, median of 7 interleaved samples, "
         "20000 calls/sample; no instrument/trace/perf, no FPS claim");
    for (unsigned packed = 0; packed <= 1; packed++) {
        for (unsigned size = 0; size < 6; size++) {
            uint64_t times[2][7];
            for (unsigned sample = 0; sample < 7; sample++) {
                for (unsigned order = 0; order < 2; order++) {
                    unsigned candidate = (order + sample) & 1;
                    /* Volatile target prevents inlining/loop hoisting. */
                    MethodFunc volatile fn = candidate ?
                        (packed ? candidate_ARRAY_ELEMENT16 :
                                  candidate_ARRAY_ELEMENT32) :
                        (packed ? original_ARRAY_ELEMENT16 :
                                  original_ARRAY_ELEMENT32);
                    seed(&d);
                    d.pgraph.draw_arrays_length = 0;
                    uint64_t start = monotonic_ns();
                    for (unsigned repeat = 0; repeat < 20000; repeat++) {
                        size_t consumed = 1;
                        d.pgraph.inline_elements_length = 0;
                        fn(&d, &d.pgraph, 3, packed ? 0x1800 : 0x1808, 0,
                           words, sizes[size], &consumed, false);
                        assert(consumed == sizes[size]);
                    }
                    times[candidate][sample] =
                        (monotonic_ns() - start) / 20000;
                }
            }
            for (unsigned version = 0; version < 2; version++) {
                qsort(times[version], 7, sizeof(uint64_t), compare_u64);
            }
            printf("%s %u words: old %llu (%llu..%llu), "
                   "new %llu (%llu..%llu)\n",
                   packed ? "index16" : "index32", sizes[size],
                   (unsigned long long)times[0][3],
                   (unsigned long long)times[0][0],
                   (unsigned long long)times[0][6],
                   (unsigned long long)times[1][3],
                   (unsigned long long)times[1][0],
                   (unsigned long long)times[1][6]);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (!strcmp(argv[1], "benchmark")) {
            benchmark();
            return 0;
        }
        return invalid(argv[1]);
    }
    regressions();
    return 0;
}
