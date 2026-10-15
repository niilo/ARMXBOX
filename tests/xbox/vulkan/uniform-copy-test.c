/*
 * Differential uniform copies through production packing/reflection helpers.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ROUND_UP(a, b) (((a) + (b) - 1) / (b) * (b))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define g_malloc0_n(n, size) calloc(n, size)
#define g_malloc0(size) calloc(1, size)

typedef struct SpvReflectBlockVariable {
    const char *name;
    uint32_t offset, size, member_count;
    struct SpvReflectBlockVariable *members;
    struct { uint32_t dims_count, dims[1], stride; } array;
    struct {
        struct { uint32_t component_count; } vector;
        struct { uint32_t column_count, stride; } matrix;
    } numeric;
} SpvReflectBlockVariable;

#ifndef UNIFORM_COPY_BENCHMARK
static unsigned copy_calls;
static void *observed_memcpy(void *dst, const void *src, size_t size)
{
    copy_calls++;
    return memcpy(dst, src, size);
}
#define memcpy observed_memcpy
#endif
#include "uniform-copy-functions.inc"
#ifndef UNIFORM_COPY_BENCHMARK
#undef memcpy
#endif

#ifndef UNIFORM_COPY_BENCHMARK
static unsigned scenarios;

static void compare_copy(const ShaderUniformLayout *shape, size_t count)
{
    uint8_t input[4096], expected[4096], actual[4096];
    assert(shape->total_size <= sizeof(actual));
    for (unsigned i = 0; i < sizeof(input); i++) {
        input[i] = (uint8_t)(i * 37 + 11);
    }
    memset(expected, 0xa5, sizeof(expected));
    memset(actual, 0xa5, sizeof(actual));
    ShaderUniformLayout old = *shape, next = *shape;
    old.allocation = expected;
    next.allocation = actual;
    copy_calls = 0;
    uniform_copy_original(&old, 1, count ? input : NULL, 4, count);
    unsigned original_calls = copy_calls;
    copy_calls = 0;
    uniform_copy(&next, 1, count ? input : NULL, 4, count);
    unsigned optimized_calls = copy_calls;
    assert(!memcmp(expected, actual, sizeof(actual)));

    const ShaderUniform *u = &shape->uniforms[0];
    unsigned elements = count / u->dim_v;
    assert(original_calls == elements);
    unsigned wanted_calls = elements;
    if (count && u->stride == 4 * u->dim_v) {
        wanted_calls = 1;
    }
    if (optimized_calls != wanted_calls) {
        fputs("contiguous uniform still uses per-element copies\n", stderr);
        abort();
    }
    /* Check all padding and surrounding bytes independently of memcpy count. */
    for (unsigned byte = 0; byte < sizeof(actual); byte++) {
        bool written = false;
        for (unsigned element = 0; element < elements; element++) {
            size_t start = u->offset + element * u->stride;
            if (byte >= start && byte < start + 4 * u->dim_v) {
                written = true;
            }
        }
        if (!written) {
            assert(actual[byte] == 0xa5);
        }
    }
    /* Change one byte and repeat to catch stale/over-broad copies. */
    if (count) {
        input[4 * count - 1] ^= 0x80;
        uniform_copy_original(&old, 1, input, 4, count);
        uniform_copy(&next, 1, input, 4, count);
        assert(!memcmp(expected, actual, sizeof(actual)));
    }
    scenarios++;
}

static void counts_for_layout(ShaderUniformLayout *layout)
{
    ShaderUniform *u = &layout->uniforms[0];
    compare_copy(layout, 0);
    compare_copy(layout, u->dim_v);
    if (u->dim_a > 1) {
        compare_copy(layout, (u->dim_a - 1) * u->dim_v);
        compare_copy(layout, u->dim_a * u->dim_v);
    }
}

static void legacy_layouts(void)
{
    for (unsigned vector = 1; vector <= 4; vector++) {
        for (unsigned array = 1; array <= 5; array += 4) {
            ShaderUniform uniforms[] = {
                { .name = "value", .dim_v = vector, .dim_a = array },
                { .name = "tail", .dim_v = 1, .dim_a = 1 },
            };
            ShaderUniformLayout layout = {
                .uniforms = uniforms, .num_uniforms = ARRAY_SIZE(uniforms),
            };
            uniform_std140(&layout);
            counts_for_layout(&layout);
            /* Legacy std430 arrays have unrelated unused stride behavior. */
            if (array == 1) {
                uniform_std430(&layout);
                counts_for_layout(&layout);
            }
        }
    }
}

static void reflected_layout(unsigned vector, unsigned array, unsigned stride,
                              unsigned columns, unsigned matrix_stride)
{
    SpvReflectBlockVariable member = {
        .name = "value", .offset = 32,
        .array = { .dims_count = array > 1, .dims = { array },
                   .stride = stride },
        .numeric = {
            .vector.component_count = vector,
            .matrix = { .column_count = columns, .stride = matrix_stride },
        },
    };
    unsigned elements = array * MAX(columns, 1);
    unsigned element_stride = columns && stride ? stride / columns :
                               MAX(stride, matrix_stride);
    unsigned width = 4 * MAX(vector, 1);
    SpvReflectBlockVariable block = {
        .member_count = 1, .members = &member,
        .size = 32 + (elements - 1) * element_stride + width + 16,
    };
    ShaderUniformLayout layout = {0};
    block_to_uniforms(&block, &layout);
    assert(layout.uniforms[0].dim_a == elements);
    assert(layout.uniforms[0].stride == element_stride);
    counts_for_layout(&layout);
    if (vector == 4 && array == 192) {
        uint8_t input[3072] = {0};
        copy_calls = 0;
        uniform_copy_original(&layout, 1, input, 4, 768);
        unsigned before = copy_calls;
        copy_calls = 0;
        uniform_copy(&layout, 1, input, 4, 768);
        assert(before == 192 && copy_calls == 1);
        printf("vec4[192] operation control: %u -> %u memcpy calls\n",
               before, copy_calls);
    }
    free((char *)layout.uniforms[0].name);
    free(layout.uniforms);
    free(layout.allocation);
}

static void invalid(const char *kind)
{
    uint8_t input[256] = {0}, output[256] = {0};
    ShaderUniform u = { .dim_v = 4, .dim_a = 8, .stride = 16, .offset = 16 };
    ShaderUniformLayout layout = {
        .uniforms = &u, .num_uniforms = 1,
        .total_size = 16 + 8 * 16, .allocation = output,
    };
    size_t count = 5;
    if (!strcmp(kind, "too-many")) {
        count = 36;
    } else if (!strcmp(kind, "bounds")) {
        layout.total_size = 32;
        count = 8;
    } else {
        assert(!strcmp(kind, "partial-vector"));
    }
    uniform_copy(&layout, 1, input, 4, count);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        invalid(argv[1]);
        return 0;
    }
    legacy_layouts();
    reflected_layout(0, 1, 0, 0, 0);
    reflected_layout(4, 1, 0, 0, 0);
    reflected_layout(1, 9, 16, 0, 0);
    reflected_layout(2, 5, 16, 0, 0);
    reflected_layout(3, 5, 16, 0, 0);
    reflected_layout(4, 192, 16, 0, 0);
    reflected_layout(1, 9, 4, 0, 0);
    reflected_layout(2, 7, 8, 0, 0);
    reflected_layout(3, 7, 16, 0, 0);
    reflected_layout(4, 1, 0, 4, 16);
    reflected_layout(3, 1, 0, 3, 16);
    reflected_layout(4, 2, 64, 4, 16);
    reflected_layout(3, 3, 48, 3, 16);
    printf("uniform copy: %u byte/padding/count differential cases passed\n",
           scenarios);
    return 0;
}
#else
/* Volatile keeps benchmark output observable outside the timed copy calls. */
static volatile unsigned benchmark_sink;

__attribute__((noinline))
static void call_original(ShaderUniformLayout *layout, void *values,
                           size_t count)
{
    uniform_copy_original(layout, 1, values, 4, count);
}

__attribute__((noinline))
static void call_optimized(ShaderUniformLayout *layout, void *values,
                            size_t count)
{
    uniform_copy(layout, 1, values, 4, count);
}

static uint64_t now_ns(void)
{
    struct timespec value;
    assert(!clock_gettime(CLOCK_MONOTONIC, &value));
    return (uint64_t)value.tv_sec * 1000000000 + value.tv_nsec;
}

static uint64_t measure(void (*copy)(ShaderUniformLayout *, void *, size_t),
                         ShaderUniformLayout *layout, uint8_t *input)
{
    const unsigned iterations = 100000;
    uint64_t start = now_ns();
    for (unsigned i = 0; i < iterations; i++) {
        input[0] = (uint8_t)i;
        copy(layout, input, 768);
        __asm__ volatile("" : : "r"(layout->allocation) : "memory");
    }
    uint64_t elapsed = now_ns() - start;
    benchmark_sink += *(uint8_t *)layout->allocation;
    return elapsed / iterations;
}

int main(void)
{
    uint8_t input[3072] = {0}, output[3072];
    ShaderUniform u = { .dim_v = 4, .dim_a = 192, .stride = 16 };
    ShaderUniformLayout layout = {
        .uniforms = &u, .num_uniforms = 1,
        .total_size = sizeof(output), .allocation = output,
    };
    uint64_t old_best = UINT64_MAX, next_best = UINT64_MAX;
    for (unsigned trial = 0; trial < 5; trial++) {
        uint64_t before = measure(call_original, &layout, input);
        uint64_t after = measure(call_optimized, &layout, input);
        old_best = before < old_best ? before : old_best;
        next_best = after < next_best ? after : next_best;
    }
    printf("local CPU vec4[192] copy only, best of 5 x 100000: "
           "old=%llu new=%llu ns/call (no game/FPS inference)\n",
           (unsigned long long)old_best, (unsigned long long)next_best);
    return 0;
}
#endif
