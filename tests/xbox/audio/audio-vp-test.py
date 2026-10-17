#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Differential real MCPX voice processing, libsamplerate and PCM fixture."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import re
import shlex
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', default='build')
    parser.add_argument('--baseline-ref',
                        default='d2da241bab37c6460fc8832e8ded75d42362e739')
    parser.add_argument('--candidate-source', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--cc', default='clang-21')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--resampler', choices=('both', 'android', 'desktop'),
                        default='both')
    args = parser.parse_args()
    for platform in ('android', 'desktop'):
        if args.resampler in ('both', platform):
            run(args, platform)


def run(args, platform):
    here = Path(__file__).resolve().parent
    repo = Path(subprocess.check_output(
        ['git', 'rev-parse', '--show-toplevel'], text=True).strip())
    build = Path(args.build_dir).resolve()
    commands = json.loads((build / 'compile_commands.json').read_text())
    entry = next(x for x in commands if x['file'].endswith('/apu/vp/vp.c'))
    flags = []
    skip = False
    for arg in shlex.split(entry['command'])[1:]:
        if skip:
            skip = False
            continue
        if arg in ('-o', '-MF', '-MQ'):
            skip = True
            continue
        if arg in ('-c', '-MD') or arg.endswith('/vp/vp.c'):
            continue
        flags.append(arg)
    if 'gcc' in args.cc:
        flags = [x for x in flags if x not in ('-Wthread-safety',
                                               '-ftrivial-auto-var-init=zero')]
    output = args.output_dir or build / (
        'cloud-audio-asan' if args.sanitize else 'cloud-audio')
    output = output.resolve() / platform
    output.mkdir(parents=True, exist_ok=True)
    source = Path('hw/xbox/mcpx/apu/vp/vp.c')
    original = subprocess.check_output(
        ['git', 'show', args.baseline_ref + ':' + str(source)],
        cwd=repo, text=True)
    candidate = (args.candidate_source or repo / source).read_text()
    for label, text in (('original', original), ('candidate', candidate)):
        (output / ('vp-' + label + '.c')).write_text(text)
    compile_commands = {}
    for label in ('original', 'candidate'):
        command = shlex.split(args.cc) + flags + [
            '-O2', '-ffunction-sections', '-fdata-sections',
            '-fno-builtin-powf', '-Wno-unused-function',
            '-I' + str(repo / source.parent),
            '-DVP_TEST_SOURCE="' + str(output / ('vp-' + label + '.c')) + '"',
            str(here / 'audio-vp-test.c'), '-o', str(output / label),
            '-Wl,--gc-sections', '-Wl,--start-group',
            str(build / 'libqemuutil.a'),
            *map(str, sorted((build / 'libqom.a.p').glob('*.o'))),
            *map(str, sorted((build / 'libevent-loop-base.a.p').glob('*.o'))),
            '-Wl,--end-group', '-lsamplerate', '-lglib-2.0', '-lm', '-pthread']
        if platform == 'android':
            command += ['-DVP_TEST_ANDROID_RESAMPLER']
        if args.sanitize:
            command += ['-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer']
        compile_commands[label] = command
        subprocess.run(command, cwd=build, check=True)
    print('compiled complete actual vp.c against real APU/QEMU headers and '
          'libsamplerate:', platform, args.cc, 'sanitizers:', args.sanitize,
          flush=True)
    rng = random.Random(0x41554449)
    cases = [(seed, mode, 8, rounding)
             for rounding in range(4)
             for seed in (0, 1, 2047, 4094, 4095)
             for mode in (0, 1, 2, 3, 4, 7, 8, 12, 16, 19, 32,
                          35, 64, 67, 128, 256, 512, 519, 2050, 2051, 2054,
                          4096, 4097, 4098, 4099, 6146, 6147)]
    cases += [(rng.randrange(4096), rng.randrange(1024), 8, 0)
              for _ in range(256)]
    cases += [(0, mode, 0, 0) for mode in (4, 12)]
    original_calls = 0
    candidate_calls = 0
    samples = 0
    digest = hashlib.sha256()
    for index, (seed, mode, rounds, rounding) in enumerate(cases):
        results = []
        counts = []
        for label in ('original', 'candidate'):
            command = [str(output / label), str(seed), str(mode),
                       str(rounds), str(rounding)]
            result = subprocess.run(command, capture_output=True, timeout=60)
            if result.returncode:
                raise RuntimeError(' '.join(command) + ': ' +
                                   result.stderr.decode(errors='replace'))
            results.append(result.stdout)
            counts.append(sum(map(int, re.findall(
                r'attenuation_powf=(\d+)', result.stderr.decode()))))
        assert results[0] == results[1], (seed, mode, rounds, rounding)
        assert counts[1] <= counts[0], (seed, mode, counts)
        original_calls += counts[0]
        candidate_calls += counts[1]
        digest.update(results[1])
        samples += rounds if rounds else 4096
        if index % 128 == 0:
            print('compared', index + 1, '/', len(cases), 'cases', flush=True)
    assert candidate_calls < original_calls, 'attenuation work did not decrease'
    if args.negative_control:
        result = subprocess.run(
            [str(output / 'original'), '1234', '4', '8'],
            capture_output=True, check=True)
        old_calls = sum(map(int, re.findall(
            r'attenuation_powf=(\d+)', result.stderr.decode())))
        result = subprocess.run(
            [str(output / 'candidate'), '1234', '4', '8'],
            capture_output=True, check=True)
        new_calls = sum(map(int, re.findall(
            r'attenuation_powf=(\d+)', result.stderr.decode())))
        assert new_calls == 64
        assert old_calls == 128
        assert old_calls > 64
        print('negative control: immutable RC7 performs 128 attenuation powf '
              'calls for eight unmuted VP frames; candidate budget is 64')
    print('PASS', platform, len(cases), 'scenarios;', samples,
          'actual voice frames: byte-identical mixbins, stereo float and '
          'S16 PCM, guest voice RAM, debug/filter state, errno and FENV flags')
    print('output SHA256:', digest.hexdigest())
    print('attenuation powf calls:', original_calls, '->', candidate_calls)
    if args.benchmark:
        for label, command in compile_commands.items():
            command = [x for x in command if x != '-fno-builtin-powf']
            command[command.index('-o') + 1] = str(
                output / (label + '-benchmark'))
            command += ['-DVP_TEST_NO_INSTRUMENTATION']
            if platform == 'android':
                command += ['-fno-math-errno']
            subprocess.run(command, cwd=build, check=True)
        print('Benchmark binaries have no powf wrapper or counters; Android '
              'matches production no-math-errno behavior')
        for mode in (1024, 1028, 1032):
            times = {'original': [], 'candidate': []}
            for trial in range(7):
                labels = ('original', 'candidate') if trial % 2 == 0 else (
                    'candidate', 'original')
                for label in labels:
                    result = subprocess.run(
                        [str(output / (label + '-benchmark')),
                         '1234', str(mode), '100000'],
                        capture_output=True, check=True, timeout=60)
                    ns = int(re.search(rb'elapsed_ns=(\d+)',
                                       result.stderr)[1])
                    times[label].append(ns / 100000)
            old = statistics.median(times['original'])
            new = statistics.median(times['candidate'])
            print('BENCH', platform, 'mode', mode,
                  'median ns/voice frame', round(old, 2),
                  '->', round(new, 2), 'ratio', round(old / new, 3), times)


if __name__ == '__main__':
    main()
