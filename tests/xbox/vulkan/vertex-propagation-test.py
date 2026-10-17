#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production mirror helpers against independent byte-array contents."""
import argparse
from pathlib import Path
import re
import resource
import shlex
import subprocess


def block(source, start):
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def function(source, name):
    match = re.search(r'\b' + name + r'\s*\([^;{}]*\)\s*\{', source)
    if not match:
        raise AssertionError('missing production function: ' + name)
    return block(source, source.rfind('\n', 0, match.start()) + 1)


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--baseline-ref',
                        default='f75153dddd03880c0c6fbfea93b2dea8ceb29af9')
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    out = (args.output_dir or repo / 'build/cloud-vulkan/propagation').resolve()
    out.mkdir(parents=True, exist_ok=True)
    prefix = 'hw/xbox/nv2a/pgraph/vk/'
    vertex = (repo / prefix / 'vertex.c').read_text()
    draw = (repo / prefix / 'draw.c').read_text()
    worker = (repo / prefix / 'render_thread.c').read_text()
    command = (repo / prefix / 'command.c').read_text()
    functions = [
        function(vertex, 'pgraph_vk_finalize_vertex_ram_state'),
        function(vertex, 'pgraph_vk_init_vertex_ram_state'),
        function(vertex, 'pgraph_vk_vertex_ram_written'),
        function(vertex, 'pgraph_vk_propagate_vertex_ram'),
        function(vertex, 'trim_unchanged_vertex_ram_pages'),
        function(vertex, 'pgraph_vk_update_vertex_ram_buffer'),
        function(worker, 'process_vertex_ram_update'),
        function(draw, 'pgraph_vk_flush_all_frames'),
        function(draw, 'xemu_set_submit_frames'),
        function(draw, 'xemu_get_submit_frames'),
    ]
    start = command.index('    extern int xemu_get_submit_frames(void);')
    end = command.index('    r->command_buffer =', start)
    functions.append('static void capture_frame_depth(PGRAPHVkState *r)\n{\n' +
                     command[start:end] + '}\n')
    finish = function(draw, 'pgraph_vk_finish')
    assert 'r->num_active_frames =' not in finish, 'live depth mutation'
    assert 'pgraph_vk_propagate_vertex_ram(r, next_frame)' in finish
    debug = (repo / 'hw/xbox/nv2a/debug.h').read_text()
    start = debug.index('typedef struct VertexRamTimingWork {')
    end = debug.index('} VertexRamTimingWork;', start)
    end += len('} VertexRamTimingWork;')
    (out / 'propagation-types.inc').write_text(debug[start:end] + '\n')
    (out / 'propagation-functions.inc').write_text('\n\n'.join(functions))
    labels = [('enabled', 1, False), ('disabled', 0, False)]
    if args.negative_control:
        def original(name):
            return subprocess.check_output(
                ['git', 'show', args.baseline_ref + ':' + prefix + name],
                cwd=repo, text=True)
        old_vertex, old_draw = original('vertex.c'), original('draw.c')
        marker = 'FrameStagingState *cur_fs ='
        assert old_draw.count(marker) == 1
        start = old_draw.rfind('{', 0, old_draw.index(marker))
        old_copy = block(old_draw, start)
        (out / 'propagation-original.inc').write_text(
            function(old_vertex, 'pgraph_vk_update_vertex_ram_buffer') +
            '\nstatic void original_propagate(PGRAPHVkState *r, '
            'int next_frame)\n' + old_copy + '\n')
        labels.append(('original', 0, True))
        upload_functions = [f for f in functions
                            if 'trim_unchanged_vertex_ram_pages' not in f and
                            'void pgraph_vk_update_vertex_ram_buffer' not in f]
        upload_functions.insert(4, function(
            old_vertex, 'pgraph_vk_update_vertex_ram_buffer'))
        (out / 'propagation-upload-original.inc').write_text(
            '\n\n'.join(upload_functions))
        labels.append(('upload-original', 0, False))
        writer = function(vertex, 'pgraph_vk_update_vertex_ram_buffer')
        marker = '        if (overlaps) {\n            /* Finish can change'
        start = writer.index(marker)
        end = writer.index('        } else {', start)
        unrestored = (writer[:start] + '        if (!overlaps) {' +
                      writer[end + len('        } else {'):])
        unrestored = unrestored.replace(
            '        hwaddr requested_offset = offset;\n', '')
        (out / 'propagation-unrestored.inc').write_text(
            '\n\n'.join(unrestored if f == writer else f for f in functions))
        labels.append(('unrestored', 1, False))
    for label, enabled, original in labels:
        binary = out / label
        command = shlex.split(args.cc) + [
            '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
            '-UNDEBUG', '-DNV2A_PERF_LOG=' + str(enabled), '-I' + str(out),
            str(here / 'vertex-propagation-test.c'), '-o', str(binary)]
        if original:
            command += ['-DNEGATIVE_CONTROL']
        if label == 'upload-original':
            command += ['-DUPLOAD_NEGATIVE_CONTROL']
        if label == 'unrestored':
            command += ['-DUNRESTORED_NEGATIVE_CONTROL']
        if args.sanitize:
            command += ['-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer']
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True,
                                preexec_fn=no_core)
        (out / (label + '.log')).write_text(result.stdout + result.stderr)
        if label == 'unrestored':
            expected = 'current mirror differs from independent byte oracle'
            if result.returncode == 0 or expected not in result.stderr:
                raise RuntimeError('unrestored-range control failed: ' +
                                   result.stdout + result.stderr)
            print('negative control: retaining the trim across finish loses '
                  'source bytes changed by completion')
        elif label == 'upload-original':
            expected = 'unchanged upload caused finish'
            if result.returncode == 0 or expected not in result.stderr:
                raise RuntimeError('upload negative control failed: ' +
                                   result.stdout + result.stderr)
            print('negative control: original writer reproduced an '
                  'unnecessary unchanged-data finish')
        elif original:
            expected = 'original propagation lost multi-generation history'
            if result.returncode == 0 or expected not in result.stderr:
                raise RuntimeError('negative control failed unexpectedly: ' +
                                   result.stdout + result.stderr)
            print('negative control: original writer/rotation reproduced '
                  'lost 3-slot history')
        elif result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        else:
            print(result.stdout, end='')
            bounds = subprocess.run([str(binary), 'bounds'],
                                    capture_output=True, text=True,
                                    preexec_fn=no_core)
            if bounds.returncode == 0 or 'offset <= vram->buffer_size' not in \
                    bounds.stderr:
                raise RuntimeError('out-of-bounds zero-size upload accepted')
            print('Out-of-bounds zero-size upload rejected before '
                  'bitmap access')
            if args.benchmark and enabled:
                benchmark = subprocess.run([str(binary), 'benchmark'],
                                           capture_output=True, text=True,
                                           check=True, preexec_fn=no_core)
                (out / 'benchmark.log').write_text(benchmark.stdout)
                print(benchmark.stdout, end='')


if __name__ == '__main__':
    main()
