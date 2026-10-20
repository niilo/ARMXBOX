#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Differential tests of actual PC24 helpers against immutable RC13."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

BASELINE = '649b22697867704f6d7c1585f4fb5d574da50b15'
PACKING_BASELINE = 'bc0718444b040c114e17d707d0fe0741aefd9126'
HELPERS = tuple('helper_' + name for name in (
    'fadd_ST0_FT0', 'fmul_ST0_FT0', 'fsub_ST0_FT0', 'fsubr_ST0_FT0',
    'fdiv_ST0_FT0', 'fdivr_ST0_FT0', 'fadd_STN_ST0', 'fmul_STN_ST0',
    'fsub_STN_ST0', 'fsubr_STN_ST0', 'fdiv_STN_ST0', 'fdivr_STN_ST0',
    'fsqrt'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    sys.path.insert(0, str(here.parent / 'x87-transfer'))
    import test as transfer

    header_source = (repo / 'target/i386/tcg/x87-native24.h').read_text()
    old_header = subprocess.check_output(
        ['git', 'show', PACKING_BASELINE + ':target/i386/tcg/x87-native24.h'],
        cwd=repo, text=True)
    for name in ('x87n24_host_nearest', 'x87n24_to_float', 'x87n24_from_float'):
        if transfer.function(header_source, name) != transfer.function(old_header, name):
            raise RuntimeError('immutable admission/checked converter changed: ' + name)
    definitions = r'^#define X87N24_(?:EXP_LIMIT|FRAC_MASK|DISCARDED_MASK) .*$'
    if re.findall(definitions, header_source, re.M) != re.findall(definitions, old_header, re.M):
        raise RuntimeError('immutable PC24 operand bounds changed')

    current = (repo / 'target/i386/tcg/fpu_helper.c').read_text()
    old = subprocess.check_output(
        ['git', 'show', BASELINE + ':target/i386/tcg/fpu_helper.c'],
        cwd=repo, text=True)
    unchanged = ('fpu/softfloat.c', 'target/i386/tcg/x87-native.h',
                 'include/fpu/softfloat-types.h')
    for name in unchanged:
        data = subprocess.check_output(['git', 'show', BASELINE + ':' + name],
                                       cwd=repo)
        if (repo / name).read_bytes() != data:
            raise RuntimeError('immutable dependency changed: ' + name)
        print(name + ' SHA256=' + hashlib.sha256(data).hexdigest())

    common = '\n\n'.join(transfer.function(old, name) for name in
                         ('fpu_set_exception', 'cpu_init_fp_statuses',
                          'save_exception_flags', 'merge_exception_flags'))
    flags = '\n'.join(re.findall(r'^#define (?:FPUS_\w+|FPUC_EM|FPU_RC_\w+)'
                                 r'\s+.*$', old, re.M))

    def helpers(source, prefix):
        begin = source.index('#if !USE_NATIVE_DOUBLE_STORAGE && '
                             '!defined(USE_HARD_FPU)')
        end = source.index('/* a compared with b,', begin)
        block = source[begin:end] + '\n#endif\n'
        block += ('\n#define X87_NATIVE_BINOP(op, a, b, r) '
                  'x87_native_binop(env, op, a, b, r)\n'
                  '#define X87_NATIVE_SQRT(r) x87_native_sqrt(env, r)\n')
        div = source.index('static inline floatx80 helper_fdiv(')
        block += transfer.function(source[div:], 'helper_fdiv') + '\n'
        block += '\n\n'.join('__attribute__((noinline)) ' +
                               transfer.function(source, name)
                               for name in HELPERS)
        if prefix:
            names = HELPERS + ('helper_fdiv', 'x87_native_enabled',
                               'x87_native_raise', 'x87_native_binop',
                               'x87_native_sqrt')
            for name in names:
                block = re.sub(r'\b' + name + r'\b', prefix + name, block)
        return block

    reference = helpers(old, 'orig_')
    gate = current[current.index('#ifndef XEMU_OPT_X87_PC24'):
                   current.index('#define FPU_RC_SHIFT')]
    actual = gate + helpers(current, '')
    # Each include has its own function names, while the public gate/mode
    # macros intentionally describe the current implementation after it.
    reference += '\n#undef X87_NATIVE_BINOP\n#undef X87_NATIVE_SQRT\n'
    with tempfile.TemporaryDirectory(prefix='x87-native24-') as tmp:
        out = args.output_dir.resolve() if args.output_dir else Path(tmp)
        out.mkdir(parents=True, exist_ok=True)
        (out / 'qemu').mkdir(exist_ok=True)
        (out / 'qemu/osdep.h').write_text(transfer.OSDEP)
        (out / 'common.inc').write_text(flags + '\n' + common)
        (out / 'reference.inc').write_text(reference)
        (out / 'current.inc').write_text(actual)
        flags = shlex.split(args.cc) + [
            '-std=gnu11', '-O2', '-g', '-fno-strict-aliasing', '-fwrapv',
            '-ffp-contract=off', '-frounding-math', '-fno-fast-math',
            '-ffunction-sections', '-fdata-sections', '-UNDEBUG',
            '-I' + str(out), '-I' + str(repo / 'include'),
            '-I' + str(repo), '-I' + str(repo / 'target/i386/tcg')]
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                      '-fno-pie']
        obj = out / 'softfloat.o'
        subprocess.run(flags + ['-c', str(repo / 'fpu/softfloat.c'),
                                '-o', str(obj)], check=True)
        header = repo / 'target/i386/tcg/x87-native24.h'
        if not header.exists():
            raise RuntimeError('production x87-native24.h not yet available')
        (out / header.name).write_bytes(header.read_bytes())
        manifest = {
            'baseline': BASELINE,
            'packing_baseline': PACKING_BASELINE,
            'compiler': subprocess.check_output(shlex.split(args.cc) +
                                                ['--version'], text=True),
            'compile_flags': flags,
            'sha256': {
                name: hashlib.sha256((repo / name).read_bytes()).hexdigest()
                for name in ('target/i386/tcg/fpu_helper.c',
                             'target/i386/tcg/x87-native24.h',
                             'target/i386/tcg/fpu-profile.h',
                             'tests/xbox/x87-native24/test.py',
                             'tests/xbox/x87-native24/test.c') + unchanged
            },
        }
        (out / 'source-manifest.json').write_text(json.dumps(manifest, indent=2)
                                                   + '\n')
        def build(name, enabled):
            binary = out / name
            command = flags + [
                '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                '-DXEMU_OPT_X87_PC24=' + str(enabled),
                str(here / 'test.c'), str(obj), '-Wl,--gc-sections',
                '-lm', '-o', str(binary)]
            if args.sanitize:
                command += ['-no-pie']
            subprocess.run(command, check=True)
            return binary

        for enabled in (0, 1):
            name = 'normal-' + str(enabled)
            binary = build(name, enabled)
            for policy in (1, 0):
                label = name + '-policy-' + str(policy)
                env = dict(os.environ, XEMU_TCG_NATIVE_X87=str(policy))
                result = subprocess.run([str(binary)], env=env,
                                        capture_output=True, text=True)
                (out / (label + '.log')).write_text(result.stdout +
                                                   result.stderr)
                if result.returncode:
                    raise RuntimeError(label + '\n' + result.stdout +
                                       result.stderr)
                print(label)
                print(result.stdout, end='')
        if args.negative_control:
            original_header = header.read_text()
            missing = actual.replace(
                'fpu_set_exception(env, inexact ? FPUS_PE : 0);',
                'if (inexact) { fpu_set_exception(env, FPUS_PE); }')
            naive = original_header.replace(
                '(low & X87N24_DISCARDED_MASK)', 'false')
            flags_lost = original_header.replace('*inexact = lost;',
                                                  '*inexact = lost && false;')
            either_sticky = actual.replace(
                '& float_flag_inexact) &&\n            (env->fpus & FPUS_PE)',
                '& float_flag_inexact) ||\n            (env->fpus & FPUS_PE)')
            bad_zero = original_header.replace(
                '*low = exp ? significand : 0;', '*low = significand;')
            bad_sign = original_header.replace(
                '*high = ((bits >> 31) << 15) |', '*high = 0 |')
            if missing == actual or naive == original_header or \
                    flags_lost == original_header or either_sticky == actual or \
                    bad_zero == original_header or bad_sign == original_header:
                raise RuntimeError('negative control boundary changed')
            # An intentionally overbroad double-intermediate implementation.
            # The dedicated witness is (1 + 2^-24) + 2^-54: binary64 loses
            # the tiny addend, leaving an exact binary32 midpoint.
            begin = original_header.rfind(
                'static inline', 0, original_header.index('x87n24_binop('))
            end = original_header.index('static inline', begin + 1)
            bad_double = '''static inline enum X87Native24Result
x87n24_binop(int op, uint64_t al, uint16_t ah, uint64_t bl, uint16_t bh,
            uint64_t *low, uint16_t *high, bool *inexact, bool need_inexact)
{
    double a, b, result;
    if (!x87n24_host_nearest() || !x87n_to_double(al, ah, &a) ||
        !x87n_to_double(bl, bh, &b)) {
        return X87N24_REJECT_INPUT;
    }
    switch (op) {
    case X87N_ADD: result = a + b; break;
    case X87N_SUB: result = a - b; break;
    case X87N_MUL: result = a * b; break;
    case X87N_DIV:
        if (!b) { return X87N24_REJECT_INPUT; }
        result = a / b;
        break;
    default: return X87N24_REJECT_INPUT;
    }
    float rounded = (float)result;
    if (!x87n24_from_float(rounded, low, high)) {
        return X87N24_REJECT_RESULT;
    }
    *inexact = (double)rounded != result;
    return X87N24_HIT;
}
'''
            double_header = (original_header[:begin] + bad_double +
                             original_header[end:])
            controls = (
                ('missing-pending', missing, original_header, [],
                 'state mismatch'),
                ('operand-rounding', actual, naive, [], 'numerical mismatch'),
                ('incorrect-inexact', actual, flags_lost, [],
                 'numerical mismatch'),
                ('either-sticky', either_sticky, original_header, [],
                 'state mismatch'),
                ('bounded-zero', actual, bad_zero, [], 'packing mismatch'),
                ('bounded-sign', actual, bad_sign, [], 'packing mismatch'),
                ('double-rounding', actual, double_header,
                 ['double-rounding-witness'], 'numerical mismatch'),
            )
            for label, body, mutant_header, arguments, expected in controls:
                (out / 'current.inc').write_text(body)
                (out / header.name).write_text(mutant_header)
                binary = build(label, 1)
                result = subprocess.run(
                    [str(binary)] + arguments,
                    env=dict(os.environ, XEMU_TCG_NATIVE_X87='1'),
                    capture_output=True, text=True)
                (out / (label + '.log')).write_text(result.stdout +
                                                   result.stderr)
                if result.returncode != 1 or expected not in result.stderr:
                    raise RuntimeError('unexpected control: ' + label + '\n' +
                                       result.stdout + result.stderr)
                print('negative control ' + label + ': detected')
            (out / 'current.inc').write_text(actual)
            (out / header.name).write_text(original_header)


if __name__ == '__main__':
    main()
