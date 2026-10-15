/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hw/xbox/mcpx/apu/apu_int.h"
#include "qemu/cutils.h"
#include <fenv.h>

static unsigned long pow_calls;
static unsigned long attenuation_calls;
static bool benchmark;

static float test_powf(float x, float y)
{
    pow_calls++;
    attenuation_calls += x == 10.0f;
    return powf(x, y);
}

/* Select Android's actual SRC_LINEAR branch after host headers are parsed. */
#ifdef VP_TEST_ANDROID_RESAMPLER
#define __ANDROID__ 1
#endif
#ifndef VP_TEST_NO_INSTRUMENTATION
#define powf test_powf
#endif
#include VP_TEST_SOURCE
#undef powf
#ifdef VP_TEST_ANDROID_RESAMPLER
#undef __ANDROID__
#endif

struct config g_config;
struct McpxApuDebug g_dbg, g_dbg_cache;
int g_dbg_voice_monitor = -1;
uint64_t g_dbg_muted_voices[4];
AddressSpace address_space_memory;
static uint8_t *test_ram;
static size_t test_ram_size;

void address_space_stl_le(AddressSpace *as, hwaddr addr, uint32_t value,
                          MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr + 4 <= test_ram_size);
    stl_le_p(test_ram + addr, value);
}

void address_space_stb(AddressSpace *as, hwaddr addr, uint8_t value,
                       MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr < test_ram_size);
    test_ram[addr] = value;
}

uint32_t address_space_ldl_le(AddressSpace *as, hwaddr addr,
                              MemTxAttrs attrs, MemTxResult *result)
{
    assert(!"unexpected out of range read");
    return 0;
}

uint16_t address_space_lduw_le(AddressSpace *as, hwaddr addr,
                               MemTxAttrs attrs, MemTxResult *result)
{
    assert(!"unexpected out of range read");
    return 0;
}

uint8_t address_space_ldub(AddressSpace *as, hwaddr addr,
                           MemTxAttrs attrs, MemTxResult *result)
{
    assert(!"unexpected out of range read");
    return 0;
}

bool mcpx_apu_debug_is_muted(uint16_t v)
{
    return (g_dbg_muted_voices[v / 64] >> (v % 64)) & 1;
}

static void set_field(MCPXAPUState *d, unsigned v, unsigned off,
                      uint32_t mask, uint32_t value)
{
    uint8_t *p = d->ram_ptr + d->regs[NV_PAPU_VPVADDR] + v * NV_PAVS_SIZE + off;
    uint32_t old = ldl_le_p(p);
    stl_le_p(p, (old & ~mask) | ((value << ctz32(mask)) & mask));
}

static void emit(const void *data, size_t size)
{
    if (!benchmark) {
        assert(fwrite(data, size, 1, stdout) == 1);
    }
}

static void volume_fields(MCPXAPUState *d, unsigned v, unsigned seed)
{
    const unsigned offs[] = {NV_PAVS_VOICE_TAR_VOLA,
                             NV_PAVS_VOICE_TAR_VOLA,
                             NV_PAVS_VOICE_TAR_VOLB,
                             NV_PAVS_VOICE_TAR_VOLB,
                             NV_PAVS_VOICE_TAR_VOLC,
                             NV_PAVS_VOICE_TAR_VOLC};
    const uint32_t masks[] = {NV_PAVS_VOICE_TAR_VOLA_VOLUME0,
                              NV_PAVS_VOICE_TAR_VOLA_VOLUME1,
                              NV_PAVS_VOICE_TAR_VOLB_VOLUME2,
                              NV_PAVS_VOICE_TAR_VOLB_VOLUME3,
                              NV_PAVS_VOICE_TAR_VOLC_VOLUME4,
                              NV_PAVS_VOICE_TAR_VOLC_VOLUME5};
    uint32_t vol[8];
    for (unsigned i = 0; i < 8; i++) {
        vol[i] = (seed + i * 577) & 0xfff;
        if (i < 6) {
            set_field(d, v, offs[i], masks[i], vol[i]);
        }
    }
    set_field(d, v, NV_PAVS_VOICE_TAR_VOLA,
              NV_PAVS_VOICE_TAR_VOLA_VOLUME6_B3_0, vol[6] & 15);
    set_field(d, v, NV_PAVS_VOICE_TAR_VOLB,
              NV_PAVS_VOICE_TAR_VOLB_VOLUME6_B7_4, (vol[6] >> 4) & 15);
    set_field(d, v, NV_PAVS_VOICE_TAR_VOLC,
              NV_PAVS_VOICE_TAR_VOLC_VOLUME6_B11_8, vol[6] >> 8);
    set_field(d, v, NV_PAVS_VOICE_TAR_VOLA,
              NV_PAVS_VOICE_TAR_VOLA_VOLUME7_B3_0, vol[7] & 15);
    set_field(d, v, NV_PAVS_VOICE_TAR_VOLB,
              NV_PAVS_VOICE_TAR_VOLB_VOLUME7_B7_4, (vol[7] >> 4) & 15);
    set_field(d, v, NV_PAVS_VOICE_TAR_VOLC,
              NV_PAVS_VOICE_TAR_VOLC_VOLUME7_B11_8, vol[7] >> 8);
}

static void run_case(unsigned seed, unsigned mode, unsigned rounds)
{
    MCPXAPUState *d = g_new0(MCPXAPUState, 1);
    d->ram_size = 1 << 20;
    d->ram_ptr = g_malloc0(d->ram_size);
    test_ram = d->ram_ptr;
    test_ram_size = d->ram_size;
    d->regs[NV_PAPU_VPVADDR] = 0x1000;
    d->regs[NV_PAPU_VPSGEADDR] = 0x9000;
    stl_le_p(d->ram_ptr + 0x9000, 0x10000);
    unsigned v = (mode & 2) ? 7 : 80;
    unsigned channels = 1 + (mode & 1);
    bool multipass = mode & 4;
    d->monitor.point = (mode & 8) ? MCPX_APU_DEBUG_MON_GP :
                                  MCPX_APU_DEBUG_MON_VP;
    g_config.audio.hrtf = !!(mode & 2048);
    g_dbg_voice_monitor = (mode & 64) ? v : -1;
    memset(&g_dbg, 0, sizeof(g_dbg));
    memset(&g_dbg_cache, 0, sizeof(g_dbg_cache));
    memset(g_dbg_muted_voices, 0, sizeof(g_dbg_muted_voices));
    if (mode & 128) {
        g_dbg_muted_voices[v / 64] = UINT64_C(1) << (v % 64);
    }
    set_field(d, v, NV_PAVS_VOICE_PAR_STATE,
              NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE, 1);
    set_field(d, v, NV_PAVS_VOICE_PAR_STATE,
              NV_PAVS_VOICE_PAR_STATE_PAUSED, !!(mode & 256));
    set_field(d, v, NV_PAVS_VOICE_CFG_FMT,
              NV_PAVS_VOICE_CFG_FMT_MULTIPASS, multipass);
    set_field(d, v, NV_PAVS_VOICE_CFG_FMT,
              NV_PAVS_VOICE_CFG_FMT_STEREO, channels == 2);
    set_field(d, v, NV_PAVS_VOICE_CFG_FMT,
              NV_PAVS_VOICE_CFG_FMT_LOOP, 1);
    set_field(d, v, NV_PAVS_VOICE_CFG_FMT,
              NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE,
              NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_B16);
    set_field(d, v, NV_PAVS_VOICE_CFG_FMT,
              NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE,
              NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_S16);
    set_field(d, v, NV_PAVS_VOICE_CFG_FMT,
              NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK, channels - 1);
    set_field(d, v, NV_PAVS_VOICE_PAR_NEXT,
              NV_PAVS_VOICE_PAR_NEXT_EBO, 1023);
    set_field(d, v, NV_PAVS_VOICE_CFG_MISC,
              NV_PAVS_VOICE_CFG_MISC_FMODE, !!(mode & 16));
    set_field(d, v, NV_PAVS_VOICE_TAR_FCA,
              NV_PAVS_VOICE_TAR_FCA_FC0, 0xe000);
    set_field(d, v, NV_PAVS_VOICE_TAR_FCA,
              NV_PAVS_VOICE_TAR_FCA_FC1, 0x6000);
    set_field(d, v, NV_PAVS_VOICE_TAR_FCB,
              NV_PAVS_VOICE_TAR_FCA_FC0, 0xf000);
    set_field(d, v, NV_PAVS_VOICE_TAR_FCB,
              NV_PAVS_VOICE_TAR_FCA_FC1, 0x7000);
    set_field(d, v, NV_PAVS_VOICE_TAR_PITCH_LINK,
              NV_PAVS_VOICE_TAR_PITCH_LINK_PITCH,
              (mode & 32) ? (uint16_t)-2048 : 0);
    if (mode & 4096) {
        set_field(d, v, NV_PAVS_VOICE_CFG_FMT,
                  NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE,
                  NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_ADPCM);
        set_field(d, v, NV_PAVS_VOICE_CFG_FMT,
                  NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE,
                  NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_S24);
    }
    float hrir[2][HRTF_NUM_TAPS] = {0};
    for (unsigned ch = 0; ch < 2; ch++) {
        for (unsigned tap = 0; tap < HRTF_NUM_TAPS; tap++) {
            hrir[ch][tap] = (int)((seed + tap * 17 + ch * 97) % 129) - 64;
        }
    }
    hrtf_filter_init(&d->vp.filters[v].hrtf);
    hrtf_filter_set_target_params(&d->vp.filters[v].hrtf, hrir,
                                  (int)(seed % 41) - 20.5f);
    volume_fields(d, v, seed);
    const uint32_t bin_masks[] = { NV_PAVS_VOICE_CFG_VBIN_V0BIN,
                                  NV_PAVS_VOICE_CFG_VBIN_V1BIN,
                                  NV_PAVS_VOICE_CFG_VBIN_V2BIN,
                                  NV_PAVS_VOICE_CFG_VBIN_V3BIN,
                                  NV_PAVS_VOICE_CFG_VBIN_V4BIN,
                                  NV_PAVS_VOICE_CFG_VBIN_V5BIN };
    for (unsigned i = 0; i < 6; i++) {
        set_field(d, v, NV_PAVS_VOICE_CFG_VBIN, bin_masks[i],
                  (seed + i * 5) % NUM_MIXBINS);
    }
    set_field(d, v, NV_PAVS_VOICE_CFG_FMT,
              NV_PAVS_VOICE_CFG_FMT_V6BIN, (seed + 4) % NUM_MIXBINS);
    set_field(d, v, NV_PAVS_VOICE_CFG_FMT,
              NV_PAVS_VOICE_CFG_FMT_V7BIN, (seed + 9) % NUM_MIXBINS);
    set_field(d, v, NV_PAVS_VOICE_TAR_PITCH_LINK,
              NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE, 0xffff);
    for (unsigned i = 0; i < 2048; i++) {
        stw_le_p(d->ram_ptr + 0x10000 + i * 2,
                  (uint16_t)(seed * 1289 + i * 377));
    }
    if (mode & 4096) {
        for (unsigned b = 0; b < 16; b++) {
            uint8_t *block = d->ram_ptr + 0x10000 + b * 36 * channels;
            for (unsigned i = 0; i < 36 * channels; i++) {
                block[i] = (seed + b * 17 + i * 3) & 255;
            }
            for (unsigned ch = 0; ch < channels; ch++) {
                stw_le_p(block + ch * 4, seed * 13 + b * 7 + ch * 97);
                block[ch * 4 + 2] = seed % 89;
                block[ch * 4 + 3] = 0;
            }
        }
    }
    for (unsigned i = 0; i < NUM_MIXBINS; i++) {
        d->vp.submix_headroom[i] = i % 5;
    }
    d->vp.hrtf_headroom = 3;
    for (unsigned i = 0; i < 4; i++) {
        d->vp.hrtf_submix[i] = (i * 3 + seed) % NUM_MIXBINS;
    }
    unsigned long calls_before = attenuation_calls;
    int list = (mode & 512) ?
        NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_MP_TOP - 1 : 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (unsigned round = 0; round < rounds; round++) {
        float mix[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
        float output[NUM_SAMPLES_PER_FRAME][2] = {0};
        for (unsigned b = 0; b < NUM_MIXBINS; b++) {
            for (unsigned s = 0; s < NUM_SAMPLES_PER_FRAME; s++) {
                mix[b][s] = ((int)(seed + b * 7 + s * 13) % 1024 - 512) /
                            1024.0f;
            }
        }
        feclearexcept(FE_ALL_EXCEPT);
        errno = 0;
        voice_process(d, mix, output, v, list);
        int exceptions = fetestexcept(FE_ALL_EXCEPT);
        int saved_errno = errno;
        emit(mix, sizeof(mix));
        emit(output, sizeof(output));
        int16_t pcm[NUM_SAMPLES_PER_FRAME][2];
        src_float_to_short_array((float *)output, (short *)pcm,
                                 NUM_SAMPLES_PER_FRAME * 2);
        emit(pcm, sizeof(pcm));
        emit(d->ram_ptr + 0x1000 + v * NV_PAVS_SIZE, NV_PAVS_SIZE);
        emit(&g_dbg.vp.v[v], sizeof(g_dbg.vp.v[v]));
        emit(&d->vp.filters[v].hrtf, sizeof(HrtfFilter));
        emit(&exceptions, sizeof(exceptions));
        emit(&saved_errno, sizeof(saved_errno));
        for (unsigned ch = 0; ch < 2; ch++) {
            emit(&d->vp.filters[v].svf[ch], offsetof(sv_filter, op));
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t ns = (t1.tv_sec - t0.tv_sec) * UINT64_C(1000000000) +
                  t1.tv_nsec - t0.tv_nsec;
    unsigned long calls = attenuation_calls - calls_before;
    fprintf(stderr, "case seed=%u mode=%u rounds=%u attenuation_powf=%lu\n",
            seed, mode, rounds, calls);
    if (benchmark) {
        fprintf(stderr, "benchmark rounds=%u elapsed_ns=%" PRIu64 "\n",
                rounds, ns);
    }
    if (d->vp.filters[v].resampler) {
        src_delete(d->vp.filters[v].resampler);
    }
    g_free(d->ram_ptr);
    g_free(d);
}

static unsigned parse_unsigned(const char *text)
{
    unsigned long value;
    assert(qemu_strtoul(text, NULL, 0, &value) == 0);
    assert(value <= UINT_MAX);
    return value;
}

int main(int argc, char **argv)
{
    assert(argc == 4 || argc == 5);
    unsigned seed = parse_unsigned(argv[1]);
    unsigned mode = parse_unsigned(argv[2]);
    unsigned rounds = parse_unsigned(argv[3]);
    const int rounding[] = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD,
                             FE_TOWARDZERO };
    unsigned choice = argc > 4 ? parse_unsigned(argv[4]) : 0;
    assert(choice < ARRAY_SIZE(rounding));
    assert(fesetround(rounding[choice]) == 0);
    benchmark = mode & 1024;
    if (rounds == 0) {
        for (unsigned i = 0; i < 4096; i++) {
            run_case(i, mode, 1);
        }
    } else {
        run_case(seed, mode, rounds);
    }
    return 0;
}
