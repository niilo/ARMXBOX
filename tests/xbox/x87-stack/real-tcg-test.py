#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute actual raw x87 emitter branches through native QEMU TCG codegen.

Uses a configured native x86-64 QEMU build for generated headers/libqemuutil.
Compiles only eight TCG source objects, not the emulator or firmware. Fixtures
select the ARM floatx80 type and the raw branch, but do not replace integer TCG
ops, optimization, register allocation, machine code generation or execution.
"""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess

BASELINE = 'fa8b312c49292e60c53f5f8221a33d4223e282bb'
NAMES = ('fpush', 'fpop', 'fmov_FT0_STN', 'fmov_ST0_STN',
         'fmov_STN_ST0', 'fxchg_ST0_STN')
SOURCES = ('tcg/tcg.c', 'tcg/tcg-op.c', 'tcg/optimize.c', 'tcg/region.c',
           'tcg/tcg-common.c', 'tcg/tcg-op-vec.c', 'tcg/tier1-opt.c',
           'tcg/tcg-op-fp.c')

# These services are linked because backend tables reference them. None is
# exercised by these integer CPU-state loads/stores. Any unexpected use aborts.
# The perf callback is intentionally a no-op; no profiler is enabled.
STUBS = r'''
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#define UNUSED_SERVICE(name) void name(void) { abort(); }
UNUSED_SERVICE(disas)
UNUSED_SERVICE(thread_context_create_thread)
UNUSED_SERVICE(helper_ldub_mmu)
UNUSED_SERVICE(helper_lduw_mmu)
UNUSED_SERVICE(helper_ldul_mmu)
UNUSED_SERVICE(helper_ldq_mmu)
UNUSED_SERVICE(helper_ld16_mmu)
UNUSED_SERVICE(helper_ldsb_mmu)
UNUSED_SERVICE(helper_ldsw_mmu)
UNUSED_SERVICE(helper_ldsl_mmu)
UNUSED_SERVICE(helper_stb_mmu)
UNUSED_SERVICE(helper_stw_mmu)
UNUSED_SERVICE(helper_stl_mmu)
UNUSED_SERVICE(helper_stq_mmu)
UNUSED_SERVICE(helper_st16_mmu)
UNUSED_SERVICE(dup_const)
void perf_report_prologue(const void *start, size_t size) { }
/* Fixed guest page configuration, matching i386-softmmu. No MMU op is used. */
#include "exec/page-vary.h"
const TargetPageBits target_page = { true, 12, -4096ULL };
'''


def balanced(text, pos):
    depth = 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return pos


def function(text, name):
    pattern = (r'^(?:static\s+)?(?:inline\s+)?[A-Za-z_]\w*\s+' +
               re.escape(name) + r'\s*\([^;{}]*\)\s*\{')
    match = re.search(pattern, text, re.M)
    if not match:
        raise RuntimeError('Function declaration not found: ' + name)
    pos = text.index('{', match.start()) + 1
    return text[match.start():balanced(text, pos)]


def extract(repo, out, baseline):
    source = (repo / 'target/i386/tcg/translate.c').read_text()
    old = subprocess.check_output(
        ['git', 'show', baseline + ':target/i386/tcg/fpu_helper.c'],
        cwd=repo, text=True)
    helpers = ('fpush', 'fpop') + tuple('helper_' + n for n in NAMES)
    helpers += ('helper_enter_mmx', 'helper_emms')
    reference = '\n\n'.join(function(old, name) for name in helpers)
    reference = reference.replace('#if USE_NATIVE_DOUBLE_STORAGE', '#if 0')
    (out / 'reference.inc').write_text(reference + '\n')
    parts = [function(source, name) for name in
             ('gen_stn_ptr', 'gen_ft0_ptr', 'gen_set_fptag',
              'fpu_using_double_precision')]
    # Instantiate the actual FP cache flush functions for both precisions.
    ops = (repo / 'target/i386/ops_fpu.h').read_text()
    for prec in (32, 64):
        for name in ('nds_store_fp', 'flush_fp_regs'):
            marker = 'static void glue(' + name + ', PREC_SUFFIX)'
            start = ops.index(marker)
            pos = ops.index('{', start) + 1
            text = ops[start:balanced(ops, pos)]
            text = text.replace('glue(' + name + ', PREC_SUFFIX)',
                                name + '_f' + str(prec))
            text = text.replace('glue(nds_store_fp, PREC_SUFFIX)',
                                'nds_store_fp_f' + str(prec))
            text = text.replace('PREC_TYPE', 'TCGv_f' + str(prec))
            text = text.replace('tcg_gen_st80f_fp',
                                'tcg_gen_st80f_f' + str(prec))
            text = text.replace('#if PREC == 64',
                                '#if ' + str(prec) + ' == 64')
            parts.append(text)
    parts.append('#define fp_pc_wrapper(f) (fpu_using_double_precision(s) ? '
                 'glue(f, _f64) : glue(f, _f32))')
    parts += [function(source, name) for name in
              ('gen_flush_fip', 'gen_flush_fp', 'gen_x87_raw_move')]
    for name in NAMES:
        text = function(source, 'gen_' + name)
        marker = '    if (use_raw_x87_stack()) {'
        begin = text.index(marker) + len(marker)
        end = balanced(text, begin)
        # Fixture-only gate override: preserve the complete selected body.
        parts.append(text[:text.index('{') + 1] + text[begin:end - 1] + '\n}')
    parts.append(function(source, 'gen_bb_epilogue'))
    body = '\n\n'.join(parts) + '\n'
    (out / 'emitters.inc').write_text(body)
    manifest = {
        'baseline_helpers': baseline,
        'candidate_translate_sha256':
            hashlib.sha256(source.encode()).hexdigest(),
        'candidate_extracted_sha256': hashlib.sha256(body.encode()).hexdigest(),
        'host': platform.machine(),
        'coverage': 'Actual TCG integer JIT and CPU state; fixture raw branch '
                    'selection and ARM floatx80 type; no guest decoder or MMU.',
    }
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return body


def compile_actual(repo, build, out, commands, source):
    row = next(r for r in commands
               if r.get('output', '').startswith('libsystem.a.p/') and
               r['file'] == '../' + source)
    args = shlex.split(row['command'])
    clean = []
    i = 0
    while i < len(args):
        if args[i] in ('-o', '-MF', '-MQ'):
            i += 2
        elif args[i] in ('-MD', '-O0', '-g', '-c') or args[i] == '../' + source:
            i += 1
        else:
            clean.append(args[i])
            i += 1
    target = out / (source.replace('/', '_') + '.o')
    command = clean + ['-O2', '-g', '-ffunction-sections', '-fdata-sections',
                       '-c', str(repo / source), '-o', str(target)]
    subprocess.run(command, cwd=build, check=True)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--baseline-ref', default=BASELINE)
    parser.add_argument('--negative-control', action='store_true')
    args = parser.parse_args()
    if platform.machine() != 'x86_64':
        raise RuntimeError('This native execution fixture currently '
                           'requires x86_64')
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    build = (args.build_dir or repo / 'build').resolve()
    out = (args.output_dir or build / 'tests/xbox/x87-stack/real-tcg').resolve()
    out.mkdir(parents=True, exist_ok=True)
    commands = json.loads((build / 'compile_commands.json').read_text())
    body = extract(repo, out, args.baseline_ref)
    (out / 'stubs.c').write_text(STUBS)
    objects = [compile_actual(repo, build, out, commands, source)
               for source in SOURCES]
    compiler = shlex.split(next(
        r['command'] for r in commands
        if r.get('output') == 'libsystem.a.p/tcg_tcg.c.o'))[0]
    flags = [compiler, '-std=gnu11', '-O2', '-g', '-fno-strict-aliasing',
             '-fwrapv', '-ffunction-sections', '-fdata-sections', '-UNDEBUG',
             '-D_GNU_SOURCE', '-DXBOX=1', '-DCOMPILING_PER_TARGET',
             '-DCONFIG_TARGET="i386-softmmu-config-target.h"']
    if shutil.which('ld.lld'):
        flags.append('-fuse-ld=lld')
    for include in (out, build, repo, repo / 'include', repo / 'target/i386',
                    build / 'target/i386', repo / 'host/include/x86_64',
                    repo / 'host/include/generic', repo / 'tcg/i386'):
        flags.append('-I' + str(include))
    flags += shlex.split(subprocess.check_output(
        ['pkg-config', '--cflags', 'glib-2.0'], text=True))
    link = [str(here / 'real-tcg-test.c'), str(out / 'stubs.c')]
    link += [str(path) for path in objects]
    link += [str(build / 'libqemuutil.a'), '-Wl,--gc-sections',
             '-pthread', '-lm']
    link += shlex.split(subprocess.check_output(
        ['pkg-config', '--libs', 'glib-2.0'], text=True))
    variants = [('normal', body, [])]
    if args.negative_control:
        lost_high = body.replace('tcg_gen_st_i64(hi, dst, 8);', '')
        if lost_high == body:
            raise RuntimeError('Negative-control boundary changed')
        variants += [('lost-high', lost_high, []),
                     ('stale-global', body, ['-DNEGATIVE_NO_SYNC=1'])]
    for label, emitters, extra in variants:
        (out / 'emitters.inc').write_text(emitters)
        binary = out / label
        command = flags + extra + link + ['-o', str(binary)]
        (out / (label + '-command.json')).write_text(
            json.dumps(command, indent=2) + '\n')
        subprocess.run(command, cwd=build, check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        (out / (label + '.log')).write_text(result.stdout + result.stderr)
        if label == 'normal':
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            print(result.stdout, end='')
        elif result.returncode == 1 and 'state mismatch' in result.stderr:
            print('Negative control ' + label + ': detected')
        else:
            raise RuntimeError('Unexpected negative control ' + label + ': ' +
                               result.stdout + result.stderr)
    (out / 'emitters.inc').write_text(body)


if __name__ == '__main__':
    main()
