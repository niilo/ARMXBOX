/*
 * Crosscheck and benchmark swizzle.
 *
 * Copyright (c) 2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define GUARD_SIZE 32
#define SENTINEL 0xa5
#define NUM_SAMPLES 11

#define PROTO(name) \
    void name(const uint8_t *src_buf, unsigned int width, \
              unsigned int height, unsigned int depth, uint8_t *dst_buf, \
              unsigned int row_pitch, unsigned int slice_pitch, \
              unsigned int bytes_per_pixel)

typedef PROTO((*SwizzleHandler));
PROTO(swizzle_box_A);
PROTO(unswizzle_box_A);
#ifdef SWIZZLE_COMPARE_BASELINE
PROTO(swizzle_box_B);
PROTO(unswizzle_box_B);
#endif

static const struct Method {
    const char *name;
    SwizzleHandler swizzle;
    SwizzleHandler unswizzle;
} methods[] = {
    { "candidate", swizzle_box_A, unswizzle_box_A },
#ifdef SWIZZLE_COMPARE_BASELINE
    { "baseline", swizzle_box_B, unswizzle_box_B },
#endif
};

typedef struct Buffer {
    uint8_t *allocation;
    uint8_t *data;
    size_t size;
    size_t prefix;
} Buffer;

static Buffer buffer_new(size_t size, unsigned int alignment_offset)
{
    Buffer b = { .size = size, .prefix = GUARD_SIZE + alignment_offset };

    b.allocation = malloc(b.prefix + size + GUARD_SIZE);
    assert(b.allocation);
    memset(b.allocation, SENTINEL, b.prefix + size + GUARD_SIZE);
    b.data = b.allocation + b.prefix;
    return b;
}

static void buffer_check_guards(Buffer *b)
{
    for (size_t i = 0; i < b->prefix; i++) {
        assert(b->allocation[i] == SENTINEL);
    }
    for (size_t i = 0; i < GUARD_SIZE; i++) {
        assert(b->data[b->size + i] == SENTINEL);
    }
}

/* Coordinate interleaving independent of the implementation's offset masks. */
static size_t morton_offset(unsigned int x, unsigned int y, unsigned int z,
                            unsigned int width, unsigned int height,
                            unsigned int depth)
{
    size_t offset = 0;
    unsigned int output_bit = 0;

    for (unsigned int bit = 1; bit < width || bit < height || bit < depth;
         bit <<= 1) {
        if (bit < width) {
            offset |= (size_t)!!(x & bit) << output_bit++;
        }
        if (bit < height) {
            offset |= (size_t)!!(y & bit) << output_bit++;
        }
        if (bit < depth) {
            offset |= (size_t)!!(z & bit) << output_bit++;
        }
    }
    return offset;
}

static uint32_t random_state = 1337;

static uint8_t random_byte(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void crosscheck_case(unsigned int width, unsigned int height,
                            unsigned int depth, unsigned int bpp,
                            unsigned int row_padding,
                            unsigned int slice_padding,
                            unsigned int alignment_offset)
{
    unsigned int row_pitch = width * bpp + row_padding;
    unsigned int slice_pitch = row_pitch * height + slice_padding;
    size_t linear_size = (size_t)slice_pitch * depth;
    size_t swizzled_size = (size_t)width * height * depth * bpp;
    Buffer linear = buffer_new(linear_size, alignment_offset);
    Buffer expected_linear = buffer_new(linear_size, alignment_offset);
    Buffer expected_swizzled = buffer_new(swizzled_size, alignment_offset);
    Buffer swizzled = buffer_new(swizzled_size, alignment_offset);
    Buffer unswizzled = buffer_new(linear_size, alignment_offset);

    for (size_t i = 0; i < linear_size; i++) {
        linear.data[i] = random_byte();
    }
    for (unsigned int z = 0; z < depth; z++) {
        for (unsigned int y = 0; y < height; y++) {
            for (unsigned int x = 0; x < width; x++) {
                size_t src = (size_t)z * slice_pitch + y * row_pitch + x * bpp;
                size_t dst = morton_offset(x, y, z, width, height, depth) * bpp;

                assert(dst + bpp <= swizzled_size);
                memcpy(expected_swizzled.data + dst, linear.data + src, bpp);
                memcpy(expected_linear.data + src, linear.data + src, bpp);
            }
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(methods); i++) {
        memset(swizzled.data, SENTINEL, swizzled_size);
        memset(unswizzled.data, SENTINEL, linear_size);
        methods[i].swizzle(linear.data, width, height, depth, swizzled.data,
                           row_pitch, slice_pitch, bpp);
        assert(!memcmp(swizzled.data, expected_swizzled.data, swizzled_size));

        /* Use oracle output so paired inverse mistakes cannot cancel out. */
        methods[i].unswizzle(expected_swizzled.data, width, height, depth,
                             unswizzled.data, row_pitch, slice_pitch, bpp);
        assert(!memcmp(unswizzled.data, expected_linear.data, linear_size));
        buffer_check_guards(&linear);
        buffer_check_guards(&expected_swizzled);
        buffer_check_guards(&swizzled);
        buffer_check_guards(&unswizzled);
    }
    free(unswizzled.allocation);
    free(swizzled.allocation);
    free(expected_swizzled.allocation);
    free(expected_linear.allocation);
    free(linear.allocation);
}

static void crosscheck(void)
{
    static const unsigned int dims[] = { 1, 2, 4, 8, 16, 32 };
    static const unsigned int bpps[] = { 1, 2, 3, 4, 8 };
    static const unsigned int padding[] = { 0, 1, 3 };
    static const unsigned int alignments[] = { 0, 1, 3, 15 };
    static const unsigned int large[][2] = {
        { 64, 64 }, { 128, 32 }, { 32, 128 }, { 256, 256 },
        { 1024, 16 }, { 16, 1024 }, { 512, 512 }, { 1024, 1024 },
    };
    size_t cases = 0;

    for (size_t w = 0; w < ARRAY_SIZE(dims); w++) {
        for (size_t h = 0; h < ARRAY_SIZE(dims); h++) {
            for (size_t d = 0; d < ARRAY_SIZE(dims); d++) {
                for (size_t b = 0; b < ARRAY_SIZE(bpps); b++) {
                    for (size_t p = 0; p < ARRAY_SIZE(padding); p++) {
                        for (size_t a = 0; a < ARRAY_SIZE(alignments); a++) {
                            crosscheck_case(dims[w], dims[h], dims[d], bpps[b],
                                            padding[p], padding[(p + a) % 3],
                                            alignments[a]);
                            cases++;
                        }
                    }
                }
            }
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(large); i++) {
        for (size_t b = 0; b < ARRAY_SIZE(bpps); b++) {
            for (size_t a = 0; a < ARRAY_SIZE(alignments); a++) {
                crosscheck_case(large[i][0], large[i][1], 1, bpps[b],
                                padding[a % 3], padding[(a + 1) % 3],
                                alignments[a]);
                cases++;
            }
        }
    }
    fprintf(stderr, "crosscheck: %zu cases passed for %zu implementation(s)\n",
            cases, ARRAY_SIZE(methods));
}

static uint64_t time_ns(void)
{
    struct timespec t;
    int result = clock_gettime(CLOCK_MONOTONIC, &t);

    assert(result == 0);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

static int compare_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;

    return (x > y) - (x < y);
}

/* Keep timed output observable even when the benchmark is built with LTO. */
static volatile uint64_t benchmark_checksum;

static void bench_case(unsigned int width, unsigned int height,
                        unsigned int depth, unsigned int bpp,
                        unsigned int row_padding)
{
    unsigned int row_pitch = width * bpp + row_padding;
    unsigned int slice_pitch = row_pitch * height;
    size_t size = (size_t)width * height * depth * bpp;
    size_t linear_size = (size_t)slice_pitch * depth;
    unsigned int iterations = 8 * 1024 * 1024 / size;
    uint64_t samples[2][ARRAY_SIZE(methods)][NUM_SAMPLES];
    Buffer linear = buffer_new(linear_size, 1);
    Buffer swizzled = buffer_new(size, 3);

    iterations = iterations ? iterations : 1;
    iterations = iterations > 1024 ? 1024 : iterations;
    for (size_t i = 0; i < linear_size; i++) {
        linear.data[i] = random_byte();
    }
    for (size_t i = 0; i < size; i++) {
        swizzled.data[i] = random_byte();
    }
    for (unsigned int direction = 0; direction < 2; direction++) {
        for (unsigned int sample = 0; sample < NUM_SAMPLES + 2; sample++) {
            for (size_t index = 0; index < ARRAY_SIZE(methods); index++) {
                /* Alternate order each sample; the first two are warmups. */
                size_t m = (index + sample) % ARRAY_SIZE(methods);
                SwizzleHandler fn = direction ? methods[m].unswizzle :
                                                methods[m].swizzle;
                const uint8_t *src = direction ? swizzled.data : linear.data;
                uint8_t *dst = direction ? linear.data : swizzled.data;
                uint64_t start = time_ns();

                for (unsigned int i = 0; i < iterations; i++) {
                    fn(src, width, height, depth, dst, row_pitch, slice_pitch,
                       bpp);
                }
                uint64_t elapsed = time_ns() - start;

                if (sample >= 2) {
                    samples[direction][m][sample - 2] = elapsed;
                }
                benchmark_checksum += dst[size / 2];
            }
        }
    }
    for (unsigned int direction = 0; direction < 2; direction++) {
        for (size_t m = 0; m < ARRAY_SIZE(methods); m++) {
            uint64_t *s = samples[direction][m];

            qsort(s, NUM_SAMPLES, sizeof(*s), compare_u64);
            double median_ns = (double)s[NUM_SAMPLES / 2] / iterations;
            double gib_per_second = size * 1e9 / median_ns /
                                    (1024 * 1024 * 1024);

            printf("%s %s %ux%ux%u bpp=%u pad=%u "
                   "min=%.0f median=%.0f max=%.0f ns/op %.3f GiB/s\n",
                   methods[m].name, direction ? "unswizzle" : "swizzle",
                   width, height, depth, bpp, row_padding,
                   (double)s[0] / iterations, median_ns,
                   (double)s[NUM_SAMPLES - 1] / iterations, gib_per_second);
        }
    }
    buffer_check_guards(&linear);
    buffer_check_guards(&swizzled);
    free(swizzled.allocation);
    free(linear.allocation);
}

static void bench(void)
{
    bench_case(64, 64, 1, 4, 0);
    bench_case(256, 256, 1, 4, 0);
    bench_case(1024, 1024, 1, 4, 0);
    bench_case(1024, 16, 1, 4, 3);
    bench_case(16, 1024, 1, 4, 1);
    bench_case(256, 256, 1, 2, 0);
    bench_case(64, 64, 16, 4, 0);
}

int main(int argc, char **argv)
{
    bool run_check = argc == 1;
    bool run_bench = argc == 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--check")) {
            run_check = true;
        } else if (!strcmp(argv[i], "--bench")) {
            run_bench = true;
        } else {
            fprintf(stderr, "usage: %s [--check] [--bench]\n", argv[0]);
            return 1;
        }
    }
    if (run_check) {
        crosscheck();
    }
    if (run_bench) {
        bench();
    }
    return 0;
}
