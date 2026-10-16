#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual runtime profiler and selected immutable/current helpers."""
import argparse
import collections
import os
from pathlib import Path
import re
import resource
import shlex
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[3]
BASELINE = 'fa8b312c49292e60c53f5f8221a33d4223e282bb'


def function(source, name):
    match = re.search(r'^(?:static inline |static )?(?:void|bool|float32) '
                      + re.escape(name) + r'\([^;{]*?\)\n\{', source, re.M)
    if not match:
        raise RuntimeError('function missing: ' + name)
    depth = 1
    end = match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def run(command, **kwargs):
    print('+', shlex.join(str(x) for x in command), flush=True)
    return subprocess.run(command, check=True, text=True, **kwargs)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--output-dir', default='build/cloud-fpu-profile')
    args = parser.parse_args()
    out = ROOT / args.output_dir
    out.mkdir(parents=True, exist_ok=True)
    (out / 'qemu').mkdir(exist_ok=True)
    (out / 'android').mkdir(exist_ok=True)
    (out / 'qemu/osdep.h').write_text('/* Fixture boundary: standard headers '
                                   'are already included. */\n')
    (out / 'android/log.h').write_text(
        '#define ANDROID_LOG_INFO 4\n'
        'int __android_log_print(int, const char *, const char *, ...);\n')
    source = (ROOT / 'target/i386/tcg/fpu_helper.c').read_text()
    sse = (ROOT / 'target/i386/ops_sse.h').read_text()
    old = subprocess.check_output(['git', 'show', BASELINE +
                                  ':target/i386/tcg/fpu_helper.c'],
                                 cwd=ROOT, text=True)
    old_sse = subprocess.check_output(['git', 'show', BASELINE +
                                      ':target/i386/ops_sse.h'],
                                     cwd=ROOT, text=True)
    names = ['helper_fpush', 'helper_fpop', 'helper_fmov_FT0_STN',
             'helper_fmov_ST0_STN', 'helper_fmov_STN_ST0',
             'helper_fxchg_ST0_STN']
    gate = source[source.index('#ifndef XEMU_OPT_X87_PC24'):
                  source.index('#define FPU_RC_SHIFT')]
    helpers = [gate, function(source, 'fpush'), function(source, 'fpop')]
    for name in names:
        original = function(old, name).replace(name, 'orig_' + name)
        current = function(source, name)
        helpers += ['__attribute__((noinline)) ' + original,
                    '__attribute__((noinline)) ' + current]
    helpers += [function(sse, 'float32_add_native'),
                '__attribute__((noinline)) ' +
                function(old_sse, 'helper_addss').replace(
                    'helper_addss', 'orig_helper_addss'),
                '__attribute__((noinline)) ' + function(sse, 'helper_addss'),
                function(source, 'x87_native_raise'),
                function(source, 'x87_native_binop'),
                function(source, 'x87_native_sqrt')]
    for name in ('BINOP', 'SQRT'):
        macro = re.search(r'^#define X87_NATIVE_' + name + r'[^\n]*\\\n'
                          r'(?:[^\n]*\\\n)*[^\n]*', source, re.M)
        assert macro
        helpers.append(macro[0])
    (out / 'helpers.inc').write_text('\n\n'.join(helpers) + '\n')
    (out / 'profile-core.inc').write_text(
        (ROOT / 'target/i386/tcg/fpu-profile.c').read_text())
    (out / 'fpu-profile-reasons.h').write_text(
        (ROOT / 'target/i386/tcg/fpu-profile-reasons.h').read_text())
    flags = [args.cc, '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra',
             '-Wno-unused-function', '-Wno-unused-parameter', '-I' + str(out),
             '-I' + str(ROOT / 'target/i386/tcg'), '-pthread']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                  '-fno-pie', '-no-pie']
    fixture = ROOT / 'tests/xbox/fpu-profile/profile-test.c'
    android = ['-DXBOX', '-D__ANDROID__', '-D__aarch64__']
    configs = [([], 'host-stub', False),
               (android, 'android-compiled-out', False),
               (android + ['-DXEMU_ENABLE_CPU_PROFILE=1'],
                'android-instrumented', True)]
    for defines, name, active in configs:
        for pc24 in (0, 1):
            binary = out / (name + '-pc24-' + str(pc24))
            run(flags + defines + ['-DXEMU_OPT_X87_PC24=' + str(pc24),
                                   '-DEXPECT_PROFILE_ACTIVE=' +
                                   str(int(active)), str(fixture), '-lm',
                                   '-o', str(binary)])
            run([str(binary)])
    if args.negative_control:
        original = (out / 'profile-core.inc').read_text()
        bad = original.replace('errno = saved_errno;', '(void)saved_errno;')
        assert bad != original
        (out / 'profile-core.inc').write_text(bad)
        binary = out / 'profile-negative'
        run(flags + android + ['-DXEMU_ENABLE_CPU_PROFILE=1',
                               '-DEXPECT_PROFILE_ACTIVE=1', str(fixture),
                               '-lm', '-o', str(binary)])
        result = subprocess.run(
            [str(binary)], text=True, capture_output=True,
            preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
        assert result.returncode != 0 and 'errno == EDOM' in result.stderr
        print('PASS negative control: removed errno preservation rejected',
              flush=True)
        (out / 'profile-core.inc').write_text(original)
        reason_path = out / 'fpu-profile-reasons.h'
        reasons = reason_path.read_text()
        mutants = [
            ('wrong-PC24-label', reasons.replace(
                'return pc == 0 ? XEMU_FPU_REJECT_PC24 :',
                'return pc == 0 ? XEMU_FPU_REJECT_PC64 :', 1),
             'reject[kind][expected]'),
            ('arithmetic-reexecution', re.sub(
                r'(xemu_fpu_profile_binop_reason\([^;{]*?\)\n\{)',
                r'\1\n    uint64_t rl = 0; uint16_t rh = 0; bool inex = false;'
                r'\n    (void)x87n_binop(op, a_low, a_high, b_low, b_high,'
                r' &rl, &rh, &inex);', reasons, count=1),
             'binary_attempts'),
        ]
        for label, mutant, expected in mutants:
            assert mutant != reasons
            reason_path.write_text(mutant)
            binary = out / ('profile-negative-' + label)
            run(flags + android + ['-DXEMU_ENABLE_CPU_PROFILE=1',
                                   '-DEXPECT_PROFILE_ACTIVE=1', str(fixture),
                                   '-lm', '-o', str(binary)])
            result = subprocess.run(
                [str(binary)], text=True, capture_output=True,
                preexec_fn=lambda: resource.setrlimit(
                    resource.RLIMIT_CORE, (0, 0)))
            assert result.returncode != 0 and expected in result.stderr
            print('PASS negative control:', label, 'rejected', flush=True)
        reason_path.write_text(reasons)
        helper_path = out / 'helpers.inc'
        helper_text = helper_path.read_text()
        pc24_mutants = [
            ('wrong-PC24-precision', reason_path, reasons.replace(
                'low, high, right, X87N24_DISCARDED_MASK, X87N24_EXP_LIMIT',
                'low, high, right, 0x7ff, X87N24_EXP_LIMIT', 1),
             'reject[kind][expected]'),
            ('wrong-host-environment-label', helper_path,
             helper_text.replace('XEMU_FPU_REJECT_HOST_ENV',
                                 'XEMU_FPU_REJECT_PC24_ENV'),
             'reject[kind][expected]'),
        ]
        for label, path, mutant, expected in pc24_mutants:
            original = path.read_text()
            assert mutant != original
            path.write_text(mutant)
            binary = out / ('profile-negative-' + label)
            run(flags + android + ['-DXEMU_ENABLE_CPU_PROFILE=1',
                                   '-DXEMU_OPT_X87_PC24=1',
                                   '-DEXPECT_PROFILE_ACTIVE=1', str(fixture),
                                   '-lm', '-o', str(binary)])
            result = subprocess.run(
                [str(binary)], text=True, capture_output=True,
                preexec_fn=lambda: resource.setrlimit(
                    resource.RLIMIT_CORE, (0, 0)))
            assert result.returncode != 0 and expected in result.stderr
            print('PASS negative control:', label, 'rejected', flush=True)
            path.write_text(original)
    if args.benchmark:
        binary = out / 'profile-benchmark'
        bench_flags = [x for x in flags if not x.startswith('-fsanitize=')]
        run(bench_flags + android + ['-DXEMU_ENABLE_CPU_PROFILE=1',
                                    '-DEXPECT_PROFILE_ACTIVE=1',
                                    '-DPROFILE_REAL_CLOCKS', str(fixture),
                                    '-lm', '-o', str(binary)])
        # Only this host fixture is pinned; emulator scheduling is untouched.
        allowed = os.sched_getaffinity(0)
        os.sched_setaffinity(0, {min(allowed)})
        result = run([str(binary), '5000000'], capture_output=True)
        os.sched_setaffinity(0, allowed)
        print(result.stdout, end='')
        data = collections.defaultdict(list)
        for line in result.stdout.splitlines():
            match = re.search(r'mode=(\d+).*ns_per_call=([\d.]+)', line)
            if match:
                data[int(match[1])].append(float(match[2]))
        labels = ['stack-original', 'stack-disabled', 'stack-sampled',
                  'empty-original', 'empty-disabled', 'empty-sampled']
        for mode, values in sorted(data.items()):
            print('host-only', labels[mode], 'median_ns',
                  statistics.median(values), 'range', min(values), max(values))


if __name__ == '__main__':
    main()
