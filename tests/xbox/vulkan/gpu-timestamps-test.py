#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Test real timestamp code with query stubs and a pre-fix negative control."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess


def recording_block(source):
    marker = 'r->gpu_ts_rp_counts[r->current_frame] = r->gpu_ts_rp_index;'
    index = source.index(marker)
    start = source.rfind('if (r->gpu_ts_supported)', 0, index)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--baseline-ref', default='HEAD')
    parser.add_argument('--thinlto', action='store_true')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    build = (args.build_dir or repo / 'build').resolve()
    out = build / 'cloud-vulkan/gpu-timestamps'
    out.mkdir(parents=True, exist_ok=True)
    commands = json.loads((build / 'compile_commands.json').read_text())
    entry = next(c for c in commands if c['file'].endswith('pgraph/vk/draw.c'))
    flags = []
    skip_next = False
    for arg in shlex.split(entry['command']):
        if skip_next:
            skip_next = False
            continue
        if arg in ('-o', '-MF', '-MQ', '-c'):
            skip_next = True
            continue
        if arg in ('-MD', '-O0', '-g'):
            continue
        flags.append(arg)
    flags += ['-O2', '-g0', '-ffunction-sections', '-fdata-sections',
              '-I' + str(repo / 'hw/xbox/nv2a/pgraph/vk')]
    lto = ['-flto=thin'] if args.thinlto else []
    source = repo / 'hw/xbox/nv2a/pgraph/vk/draw.c'
    original = out / 'draw-original.c'
    original.write_bytes(subprocess.check_output(
        ['git', 'show', args.baseline_ref +
         ':hw/xbox/nv2a/pgraph/vk/draw.c'], cwd=repo))
    fixture = Path(__file__).with_name('gpu-timestamps-test.c')
    for label, draw in [('candidate', source), ('original', original)]:
        dest = out / label
        dest.mkdir(exist_ok=True)
        (dest / 'gpu-timestamps-recording.h').write_text(
            'static void gpu_timestamps_recording_complete(PGRAPHVkState *r)\n'
            '{\n' + recording_block(draw.read_text()) + '\n}\n')
        obj = dest / 'test.o'
        command = flags + lto + ['-I' + str(dest),
                  '-DGPU_TIMESTAMPS_DRAW_SOURCE="' + str(draw) + '"',
                  '-c', str(fixture), '-o', str(obj)]
        with (dest / 'compile.log').open('w') as log:
            subprocess.run(command, cwd=entry['directory'], stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        binary = dest / 'test'
        subprocess.run([flags[0], '-fuse-ld=lld'] + lto + [str(obj),
                       '-Wl,--gc-sections', '-lglib-2.0', '-pthread',
                       '-o', str(binary)], check=True)
        # Running the negative control should abort on the duplicate count.
        # Suppress core dumps so expected failure leaves no large artifacts.
        import resource
        def no_core():
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        result = subprocess.run([str(binary)], text=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, preexec_fn=no_core)
        (dest / 'test.log').write_text(result.stdout)
        print(label + ':\n' + result.stdout, end='')
        if label == 'candidate' and result.returncode:
            raise SystemExit('Candidate regression failed')
        if label == 'original':
            if result.returncode == 0:
                raise SystemExit('Original source unexpectedly passed')
            if ('gpu_total_ns' not in result.stdout or
                    '(12000 == 6000)' not in result.stdout):
                raise SystemExit('Original failed for an unexpected reason')
            print('Original source rejected by duplicate-count assertion.')


if __name__ == '__main__':
    main()
