#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare real bulk index methods against their immutable per-word baseline."""
import argparse
from pathlib import Path
import re
import resource
import shlex
import subprocess


def extract(source, pattern):
    match = re.search(pattern, source)
    if not match:
        raise AssertionError('missing production definition: ' + pattern)
    start = source.rfind('\n', 0, match.start()) + 1
    brace = source.index('{', match.start())
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def function(source, name):
    return extract(source, r'\b' + name + r'\s*\([^;{}]*\)\s*\{')


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--baseline-ref',
                        default='8aa78b9cb878c8cc9f11388ec28bad1d6a3ef086')
    args = parser.parse_args()
    if args.benchmark and args.sanitize:
        parser.error('run benchmark and sanitizer checks separately')
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    out = (args.output_dir or repo / 'build/cloud-pgraph/index-batch').resolve()
    out.mkdir(parents=True, exist_ok=True)
    path = 'hw/xbox/nv2a/pgraph/pgraph.c'
    source = (repo / path).read_text()
    original = subprocess.check_output(
        ['git', 'show', args.baseline_ref + ':' + path], cwd=repo, text=True)
    reset = (repo / 'hw/xbox/nv2a/pgraph/vertex.c').read_text()
    shared = [function(reset, 'pgraph_reset_draw_arrays'),
              function(reset, 'pgraph_reset_inline_buffers'),
              function(source, 'pgraph_expand_draw_arrays'),
              function(source, 'pgraph_check_within_begin_end_block')]
    (out / 'index-shared.inc').write_text('\n\n'.join(shared))
    for label, text in [('candidate', source), ('original', original)]:
        methods = [extract(text, r'DEF_METHOD(?:_NON_INC)?\(NV097, ' +
                           name + r'\)') for name in
                   ('ARRAY_ELEMENT16', 'ARRAY_ELEMENT32')]
        if label == 'original':
            methods.insert(0, function(original, 'pgraph_method_non_inc'))
            start = original.index('#define METHOD_HANDLER_ARGS ')
            end = original.index('\n#define DEF_METHOD_PROTO', start)
            methods.insert(0, original[start:end])
        (out / ('index-' + label + '.inc')).write_text('\n\n'.join(methods))
    debug = (repo / 'hw/xbox/nv2a/debug.h').read_text()
    types = []
    for name in ('CpuTimingWork', 'CpuTimingStats'):
        start = debug.index('typedef struct ' + name + ' {')
        end = debug.index('} ' + name + ';', start) + len('} ' + name + ';')
        types.append(debug[start:end])
    (out / 'index-types.inc').write_text('\n\n'.join(types))
    profile = (repo / 'hw/xbox/nv2a/pgraph/profile.c').read_text()
    (out / 'index-profile.inc').write_text(
        function(profile, 'snapshot_cpu_timing') + '\n' +
        function(profile, 'nv2a_profile_get_cpu_timing_str'))
    variants = [(trace, perf, 1) for trace in (0, 1) for perf in (0, 1)]
    if args.benchmark:
        variants.append((0, 0, 0))
    for trace, perf, instrument in variants:
        name = f'test-{trace}-{perf}-{instrument}'
        binary = out / name
        command = shlex.split(args.cc) + [
            '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-parameter', '-UNDEBUG', '-I' + str(out),
            f'-DTRACE_NV2A_PGRAPH_METHOD_ENABLED={trace}',
            f'-DNV2A_PERF_LOG={perf}', f'-DINDEX_INSTRUMENT={instrument}',
            str(here / 'index-batch-test.c'), '-lm', '-o', str(binary)]
        if args.sanitize:
            command += ['-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer']
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True,
                                preexec_fn=no_core)
        (out / (name + '.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        print(result.stdout, end='')
        for mode in ('empty16', 'empty32', 'full16', 'full32', 'overflow16',
                     'expand-overflow16'):
            invalid = subprocess.run([str(binary), mode], capture_output=True,
                                     text=True, preexec_fn=no_core)
            (out / (name + '-' + mode + '.log')).write_text(
                invalid.stdout + invalid.stderr)
            if invalid.returncode != -6 or 'Assertion' not in invalid.stderr:
                raise RuntimeError('invalid run not rejected: ' + mode +
                                   invalid.stdout + invalid.stderr)
        old = subprocess.run([str(binary), 'original-overflow16'],
                             capture_output=True, text=True, preexec_fn=no_core)
        (out / (name + '-original-overflow16.log')).write_text(
            old.stdout + old.stderr)
        if old.returncode == 0 or 'original packed append overwrote canary' \
                not in old.stderr:
            raise RuntimeError('overflow negative control failed: ' +
                               old.stdout + old.stderr)
        print('six invalid-run assertions and original overflow control passed')
        if not instrument:
            result = subprocess.run([str(binary), 'benchmark'],
                                    capture_output=True, text=True, check=True,
                                    preexec_fn=no_core)
            (out / 'benchmark.log').write_text(result.stdout)
            print(result.stdout, end='')
            asm = out / 'index-batch.s'
            subprocess.run(command[:-2] + ['-S', '-o', str(asm)], check=True)
            print('Assembly: ' + str(asm))


if __name__ == '__main__':
    main()
