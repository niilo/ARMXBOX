#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare current transfer helpers with RC11 helpers and real softfloat."""
import argparse
import hashlib
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

BASELINE = '28c41dfe9115613248ed5a520c897b0cb6e150a6'
HELPERS = ('helper_flds_FT0', 'helper_fldl_FT0', 'helper_flds_ST0',
           'helper_fldl_ST0', 'helper_fsts_ST0', 'helper_fstl_ST0')
# This supplies standard includes/configuration and GLib assertion spellings
# only. No numerical function or softfloat type is stubbed or replaced.
OSDEP = '''
#ifndef X87_TRANSFER_TEST_OSDEP_H
#define X87_TRANSFER_TEST_OSDEP_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include "qemu/compiler.h"
#define CONFIG_INT128 1
#define CONFIG_INT128_TYPE 1
#define g_assert assert
#define g_assert_not_reached() abort()
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#endif
'''


def function(source, name):
    match = re.search(r'^(?:static\s+)?(?:inline\s+)?[A-Za-z_]\w*\s+' +
                      re.escape(name) + r'\s*\([^;{}]*\)\s*\{', source, re.M)
    if not match:
        raise RuntimeError('function missing: ' + name)
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--baseline-ref', default=BASELINE)
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    source = (repo / 'target/i386/tcg/fpu_helper.c').read_text()
    old = subprocess.check_output(
        ['git', 'show', args.baseline_ref + ':target/i386/tcg/fpu_helper.c'],
        cwd=repo, text=True)
    soft = repo / 'fpu/softfloat.c'
    old_soft = subprocess.check_output(
        ['git', 'show', args.baseline_ref + ':fpu/softfloat.c'], cwd=repo)
    if soft.read_bytes() != old_soft:
        raise RuntimeError('real softfloat differs from immutable RC11')
    print('real softfloat SHA256=' + hashlib.sha256(old_soft).hexdigest())
    common = '\n\n'.join(function(old, n) for n in
                         ('fpu_set_exception', 'cpu_init_fp_statuses',
                          'save_exception_flags', 'merge_exception_flags'))
    flags = '\n'.join(re.findall(r'^#define (?:FPUS_\w+|FPUC_EM)\s+.*$',
                                 old, re.M))
    original = '\n\n'.join('__attribute__((noinline)) ' +
                             function(old, n).replace(n, 'orig_' + n)
                             for n in HELPERS)
    current = '\n\n'.join('__attribute__((noinline)) ' + function(source, n)
                            for n in HELPERS)
    begin = source.index('#if XEMU_FPU_PROFILE_ACTIVE && X87_TRANSFER_ACTIVE')
    end = source.index('#endif', begin)
    classifiers = '#if X87_TRANSFER_ACTIVE\n' + source[
        source.index('\n', begin) + 1:end] + '#endif\n'
    current = classifiers + current
    gate = source[source.index('#ifndef XEMU_OPT_X87_TRANSFER'):
                  source.index('#define FPU_RC_SHIFT')]
    variants = [('normal', current, '', None)]
    if args.negative_control:
        missing = current.replace('        fpu_set_exception(env, 0);', '')
        if missing == current:
            raise RuntimeError('missing-status control boundary changed')
        variants += [('missing-pending', missing, 'state mismatch', None),
                     ('missing-precision', current, 'state mismatch',
                      'precision == floatx80_precision_s')]
    with tempfile.TemporaryDirectory(prefix='x87-transfer-') as tmp:
        out = args.output_dir.resolve() if args.output_dir else Path(tmp)
        out.mkdir(parents=True, exist_ok=True)
        (out / 'qemu').mkdir(exist_ok=True)
        (out / 'qemu/osdep.h').write_text(OSDEP)
        includes = ['-I' + str(out), '-I' + str(repo / 'include'),
                    '-I' + str(repo), '-I' + str(repo / 'target/i386/tcg')]
        compile_flags = shlex.split(args.cc) + [
            '-std=gnu11', '-O2', '-g', '-fno-strict-aliasing', '-fwrapv',
            '-ffunction-sections', '-fdata-sections', '-UNDEBUG'] + includes
        if args.sanitize:
            compile_flags += ['-fsanitize=address,undefined',
                              '-fno-omit-frame-pointer', '-fno-pie']
        obj = out / 'softfloat.o'
        subprocess.run(compile_flags + ['-c', str(soft), '-o', str(obj)],
                       check=True)
        (out / 'common.inc').write_text(flags + '\n' + common)
        (out / 'reference.inc').write_text(original)
        header = (repo / 'target/i386/tcg/x87-transfer.h').read_text()
        for label, text, expected, mutant in variants:
            (out / 'current.inc').write_text(gate + '\n' + text)
            if mutant:
                (out / 'x87-transfer.h').write_text(
                    header.replace(mutant, 'false'))
            else:
                (out / 'x87-transfer.h').write_text(header)
            for enabled in ((0, 1) if not expected else (1,)):
                name = label + '-' + str(enabled)
                binary = out / name
                command = compile_flags + [
                    '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-DXEMU_OPT_X87_TRANSFER=' + str(enabled),
                    str(here / 'test.c'), str(obj), '-Wl,--gc-sections',
                    '-lm', '-o', str(binary)]
                if args.sanitize:
                    command += ['-no-pie']
                subprocess.run(command, check=True)
                result = subprocess.run(
                    [str(binary)],
                    capture_output=True, text=True)
                (out / (name + '.log')).write_text(
                    result.stdout + result.stderr)
                if expected:
                    if result.returncode != 1 or expected not in result.stderr:
                        raise RuntimeError('unexpected control: ' + name +
                                           result.stdout + result.stderr)
                    print('negative control ' + name + ': detected')
                else:
                    if result.returncode:
                        raise RuntimeError(result.stdout + result.stderr)
                    print('XEMU_OPT_X87_TRANSFER=' + str(enabled))
                    print(result.stdout, end='')
        (out / 'current.inc').write_text(gate + '\n' + current)
        (out / 'x87-transfer.h').write_text(header)


if __name__ == '__main__':
    main()
