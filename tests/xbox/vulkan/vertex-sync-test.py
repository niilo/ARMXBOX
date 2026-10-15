#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production vertex binding, dirty scanning, sync and RAM copies."""
import argparse
from pathlib import Path
import re
import runpy
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--baseline-ref', default='HEAD')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    cache_path = Path(__file__).with_name('vertex-cache-test.py')
    cache = runpy.run_path(str(cache_path))
    function, definition = cache['function'], cache['definition']

    def run(original):
        def read(path):
            if original:
                return subprocess.check_output(
                    ['git', 'show', args.baseline_ref + ':' + path],
                    cwd=repo, text=True)
            return (repo / path).read_text()
        vertex = read('hw/xbox/nv2a/pgraph/vk/vertex.c')
        draw = read('hw/xbox/nv2a/pgraph/vk/draw.c')
        common = read('hw/xbox/nv2a/pgraph/vertex.c')
        arrays = [definition(vertex, r'^static const VkFormat ' + name + r'\[')
                  for name in ('float_to_count', 'ub_to_count', 's1_to_count',
                               's32k_to_count', 's32k_int_to_count')]
        formats = sorted(set(re.findall(r'VK_FORMAT_\w+', ''.join(arrays))))
        formats.append('VK_FORMAT_R32_SINT')
        binder = ['enum { ' + ', '.join(formats) + ' };'] + arrays
        binder += [function(common, 'pgraph_update_inline_value'),
                   function(common, 'pgraph_get_inline_values'),
                   function(vertex, 'update_memory_buffer')]
        if 'static void update_inline_attribute_value(' in vertex:
            binder.append(function(vertex, 'update_inline_attribute_value'))
        if 'static void update_immediate_attribute_values(' in vertex:
            binder.append(function(vertex, 'update_immediate_attribute_values'))
        binder.append(function(vertex, 'pgraph_vk_bind_vertex_attributes'))
        sync = []
        if 'void pgraph_vk_vertex_ram_written(' in vertex:
            sync.append(function(vertex, 'pgraph_vk_vertex_ram_written'))
        if 'static void trim_unchanged_vertex_ram_pages(' in vertex:
            sync.append(function(vertex, 'trim_unchanged_vertex_ram_pages'))
        sync += [function(draw, 'get_size_and_count_for_format'),
                 function(vertex, 'pgraph_vk_update_vertex_ram_buffer'),
                 function(draw, 'has_dirty_vertex_pages'),
                 function(draw, 'compare_memory_sync_requirement_by_addr'),
                 function(draw, 'sync_vertex_ram_buffer')]
        with tempfile.TemporaryDirectory(prefix='vertex-sync-') as directory:
            output = Path(directory)
            (output / 'vertex-cache-functions.inc').write_text(
                '\n\n'.join(binder) + '\n')
            (output / 'vertex-sync-functions.inc').write_text(
                '\n\n'.join(sync) + '\n')
            binary = output / 'vertex-sync'
            command = shlex.split(args.cc) + [
                '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-UNDEBUG',
                '-Wno-sign-compare', '-I' + directory,
                str(Path(__file__).with_suffix('.c')), '-o', str(binary)]
            if args.sanitize:
                command += ['-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer']
            subprocess.run(command, cwd=repo, check=True)
            result = subprocess.run([str(binary)], capture_output=True,
                                    text=True)
            if original:
                expected = 'vertex sync omitted a consumed tail byte'
                if result.returncode == 0 or expected not in result.stderr:
                    raise RuntimeError('unexpected negative control: ' +
                                       result.stdout + result.stderr)
                print('negative control: original sync omits consumed tail')
            else:
                if result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                print(result.stdout, end='')

    run(False)
    if args.negative_control:
        run(True)


if __name__ == '__main__':
    main()
