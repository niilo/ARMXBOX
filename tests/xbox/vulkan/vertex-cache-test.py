#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare the real cached vertex binder against its forced-cold path."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def definition(source, pattern):
    match = re.search(pattern, source, re.MULTILINE)
    if not match:
        raise ValueError('missing definition: ' + pattern)
    start = source.rfind('\n', 0, match.start()) + 1
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    if source[end:end + 1] == ';':
        end += 1
    return source[start:end]


def function(source, name):
    return definition(source, r'^(?:static\s+)?(?:inline\s+)?\w+\s+' + name +
                      r'\s*\([^;{}]*\)\s*\{')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--baseline-ref', default='HEAD')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]

    def run(original):
        def read(path):
            if original:
                return subprocess.check_output(
                    ['git', 'show', args.baseline_ref + ':' + path],
                    cwd=repo, text=True)
            return (repo / path).read_text()
        vertex = read('hw/xbox/nv2a/pgraph/vk/vertex.c')
        common = read('hw/xbox/nv2a/pgraph/vertex.c')
        arrays = [definition(vertex, r'^static const VkFormat ' + name + r'\[')
                  for name in ('float_to_count', 'ub_to_count', 's1_to_count',
                               's32k_to_count', 's32k_int_to_count')]
        formats = sorted(set(re.findall(r'VK_FORMAT_\w+', ''.join(arrays))))
        formats.append('VK_FORMAT_R32_SINT')
        pieces = ['enum { ' + ', '.join(formats) + ' };'] + arrays
        pieces += [function(common, 'pgraph_update_inline_value'),
                   function(common, 'pgraph_get_inline_values'),
                   function(vertex, 'update_memory_buffer')]
        if not original:
            pieces.append(function(vertex, 'update_inline_attribute_value'))
            pieces.append(function(vertex, 'update_immediate_attribute_values'))
        pieces.append(function(vertex, 'pgraph_vk_bind_vertex_attributes'))
        with tempfile.TemporaryDirectory(prefix='vertex-cache-') as directory:
            output = Path(directory)
            (output / 'vertex-cache-functions.inc').write_text(
                '\n\n'.join(pieces) + '\n')
            binary = output / 'vertex-cache'
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
                expected = 'cached inline values diverge from cold path'
                if result.returncode == 0 or expected not in result.stderr:
                    raise RuntimeError('unexpected negative control: ' +
                                       result.stdout + result.stderr)
                print('negative control: original cache retains stale '
                      'RAM-backed inline values')
            else:
                if result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                print(result.stdout, end='')

    run(False)
    if args.negative_control:
        run(True)


if __name__ == '__main__':
    main()
