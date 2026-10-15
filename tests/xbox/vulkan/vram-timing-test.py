#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the actual rotation wait/copy telemetry blocks with a fake clock."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess


def block(source, start):
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def function(source, name):
    match = re.search(r'\b' + name + r'\s*\([^;{}]*\)\s*\{', source)
    if not match:
        raise AssertionError('missing production function: ' + name)
    return block(source, source.rfind('\n', 0, match.start()) + 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    out = (args.output_dir or repo / 'build/cloud-vulkan/vram-timing').resolve()
    out.mkdir(parents=True, exist_ok=True)
    draw = (repo / 'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
    vertex = (repo / 'hw/xbox/nv2a/pgraph/vk/vertex.c').read_text()
    debug = (repo / 'hw/xbox/nv2a/debug.h').read_text()
    profile = (repo / 'hw/xbox/nv2a/pgraph/profile.c').read_text()
    types = []
    for name in ('VertexRamTimingWork', 'VertexRamTimingStats'):
        start = debug.index('typedef struct ' + name + ' {')
        end = debug.index('} ' + name + ';', start) + len('} ' + name + ';')
        types.append(debug[start:end])
    (out / 'vram-timing-types.inc').write_text('\n'.join(types) + '\n')

    marker = 'if (qatomic_read(&r->frame_submitted[next_frame])) {'
    assert draw.count(marker) == 1
    start = draw.index(marker)
    end = draw.index('gpu_ts_readback(r, next_frame);', start)
    wait = draw[start:end] + '\n}'
    (out / 'vram-timing-functions.inc').write_text(
        'static void exercise_wait(PGRAPHVkState *r, int next_frame)\n{\n' +
        wait + '\n}\n' +
        function(vertex, 'pgraph_vk_propagate_vertex_ram') + '\n')
    (out / 'vram-timing-profile.inc').write_text(
        function(profile, 'snapshot_vram_timing') + '\n' +
        function(profile, 'nv2a_profile_get_vram_timing_str') + '\n')
    for enabled in (1, 0):
        binary = out / ('vram-timing-' + str(enabled))
        command = shlex.split(args.cc) + [
            '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-UNDEBUG',
            '-DNV2A_PERF_LOG=' + str(enabled), '-I' + str(out),
            str(here / 'vram-timing-test.c'), '-lm', '-o', str(binary)]
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary)], check=True,
                                capture_output=True, text=True)
        print(result.stdout, end='')


if __name__ == '__main__':
    main()
