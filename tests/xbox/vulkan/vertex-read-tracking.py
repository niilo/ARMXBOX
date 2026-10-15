#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual vertex bind/snapshot/upload code without Vulkan or games."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def function(source, name):
    match = re.search(r'\b' + name + r'\s*\([^;{}]*\)\s*\{', source)
    if not match:
        raise ValueError(f'function definition missing: {name}')
    start = source.rfind('\n', 0, match.start()) + 1
    depth = 1
    end = match.end()
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end]


def admission(source):
    marker = 'if (is_safe && w->count < REORDER_WINDOW_MAX)'
    start = source.index(marker)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return '''static void exercise_admission(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    ReorderWindow *w = &pg->vk_renderer_state->reorder_window;
    bool is_safe = true;
''' + source[start:end] + '\n}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-control', action='store_true',
                        help='require the selected baseline source to fail')
    parser.add_argument('--baseline-ref',
                        default='f75153dddd03880c0c6fbfea93b2dea8ceb29af9',
                        help='Git revision for the optional negative control')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    draw_path = 'hw/xbox/nv2a/pgraph/vk/draw.c'
    vertex_path = 'hw/xbox/nv2a/pgraph/vk/vertex.c'

    def run(original):
        def read(path):
            if original:
                return subprocess.check_output(
                    ['git', 'show', args.baseline_ref + ':' + path],
                    cwd=repo, text=True)
            return (repo / path).read_text()
        draw, vertex = read(draw_path), read(vertex_path)
        formats = function(draw, 'get_size_and_count_for_format')
        names = sorted(set(re.findall(r'VK_FORMAT_\w+', formats)))
        functions = ['enum { ' + ', '.join(names) + ' };']
        if not original:
            functions.append(formats)
            functions.append(function(draw, 'mark_vertex_buffer_reads'))
            functions.append(function(vertex, 'pgraph_vk_vertex_ram_written'))
            functions.append(function(
                vertex, 'trim_unchanged_vertex_ram_pages'))
        functions += [function(draw, 'bind_vertex_buffer'),
                      function(draw, 'snapshot_vertex_buffers'),
                      function(vertex, 'pgraph_vk_update_vertex_ram_buffer'),
                      admission(draw)]
        with tempfile.TemporaryDirectory(prefix='vertex-read-') as directory:
            output = Path(directory)
            (output / 'vertex-read-functions.inc').write_text(
                '\n\n'.join(functions) + '\n')
            binary = output / 'vertex-read-tracking'
            command = shlex.split(args.cc) + [
                '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-UNDEBUG',
                '-I' + directory, str(Path(__file__).with_suffix('.c')),
                '-o', str(binary)]
            if original:
                command += ['-DNEGATIVE_CONTROL']
            if args.sanitize:
                command += ['-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer']
            subprocess.run(command, cwd=repo, check=True)
            result = subprocess.run([str(binary)], capture_output=True,
                                    text=True)
            if original:
                expected = 'recorded draw changed before submission'
                if result.returncode == 0 or expected not in result.stderr:
                    raise RuntimeError('original source did not corrupt: ' +
                                       result.stdout + result.stderr)
                print('negative control: original source reproduces '
                      'vertex overwrite')
                result = subprocess.run([str(binary), 'admission'],
                                        capture_output=True, text=True)
                expected = 'captured draw admitted into stale entry'
                if result.returncode == 0 or expected not in result.stderr:
                    raise RuntimeError('original source did not misadmit: ' +
                                       result.stdout + result.stderr)
                print('negative control: original source reproduces '
                      'stale reorder admission')
            else:
                if result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                print(result.stdout, end='')

    run(False)
    if args.negative_control:
        run(True)


if __name__ == '__main__':
    main()
