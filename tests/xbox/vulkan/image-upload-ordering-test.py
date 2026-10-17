#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Capture the real image transition helper's Vulkan synchronization scopes."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def function(source, name):
    match = re.search(r'\b' + name + r'\s*\([^;{}]*\)\s*\{', source)
    if not match:
        raise ValueError('missing production function: ' + name)
    start = source.rfind('\n', 0, match.start()) + 1
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--baseline-ref',
                        default='d2da241bab37c6460fc8832e8ded75d42362e739')
    parser.add_argument('--vulkan-include', type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    vulkan_include = args.vulkan_include or (
        repo / 'thirdparty/SDL2/src/video/khronos')
    assert (vulkan_include / 'vulkan/vulkan.h').is_file()
    path = 'hw/xbox/nv2a/pgraph/vk/image.c'
    names = ('check_format_has_depth_component',
             'check_format_has_stencil_component',
             'pgraph_vk_transition_image_layout')
    for original in (False, True) if args.negative_control else (False,):
        source = (subprocess.check_output(
            ['git', 'show', args.baseline_ref + ':' + path], cwd=repo,
            text=True) if original else (repo / path).read_text())
        with tempfile.TemporaryDirectory(prefix='image-upload-order-') as temp:
            output = Path(temp)
            (output / 'image-transition-functions.inc').write_text(
                '\n\n'.join(function(source, name) for name in names) + '\n')
            binary = output / 'fixture'
            command = shlex.split(args.cc) + [
                '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-parameter', '-UNDEBUG',
                '-I' + str(vulkan_include), '-I' + temp,
                str(here / 'image-upload-ordering-test.c'), '-o', str(binary)]
            if args.sanitize:
                command += ['-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer']
            subprocess.run(command, check=True)
            result = subprocess.run([str(binary)], capture_output=True,
                                    text=True)
            if original:
                expected = ('GENERAL upload does not order prior '
                            'fragment sampling')
                if result.returncode == 0 or expected not in result.stderr:
                    raise RuntimeError('unexpected negative control: ' +
                                       result.stdout + result.stderr)
                unchanged = subprocess.run(
                    [str(binary), '--unchanged-only'], capture_output=True,
                    text=True, check=True)
                print(unchanged.stdout, end='')
                print('negative control: immutable RC7 transition omits '
                      'fragment sampling before a GENERAL image upload')
            elif result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            else:
                print(result.stdout, end='')


if __name__ == '__main__':
    main()
