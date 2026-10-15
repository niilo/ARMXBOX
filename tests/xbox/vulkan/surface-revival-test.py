#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production surface revival with CPU writes during shelving."""
import argparse
from pathlib import Path
import re
import resource
import shlex
import subprocess
import tempfile


def disable_core_dumps():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def function(source, name):
    match = re.search(r'(?m)^(?:static\s+)?(?:inline\s+)?\w+\s+\**' +
                      name + r'\s*\([^;{}]*\)\s*\{', source)
    if not match:
        raise ValueError('missing function: ' + name)
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--baseline-ref',
                        default='d2da241bab37c6460fc8832e8ded75d42362e739')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    path = 'hw/xbox/nv2a/pgraph/vk/surface.c'
    names = ('download_surface', 'shelve_surface', 'get_shelved_surface',
             'migrate_surface_image', 'update_surface_part')

    for original in (False, True) if args.negative_control else (False,):
        if original:
            source = subprocess.check_output(
                ['git', 'show', args.baseline_ref + ':' + path],
                cwd=repo, text=True)
        else:
            source = (repo / path).read_text()
        with tempfile.TemporaryDirectory(prefix='surface-revival-') as temp:
            output = Path(temp)
            (output / 'surface-revival-functions.inc').write_text(
                '\n\n'.join(function(source, name) for name in names) + '\n')
            binary = output / 'surface-revival'
            command = shlex.split(args.cc) + [
                '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-parameter', '-Wno-sign-compare', '-UNDEBUG',
                '-I' + str(repo / 'include'), '-I' + temp,
                str(Path(__file__).with_suffix('.c')), '-o', str(binary)]
            if args.sanitize:
                command += ['-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer']
            subprocess.run(command, cwd=repo, check=True)
            result = subprocess.run([str(binary)], capture_output=True,
                                    text=True, preexec_fn=disable_core_dumps)
            if original:
                expected = 'revived surface skipped authoritative VRAM upload'
                if result.returncode == 0 or expected not in result.stderr:
                    raise RuntimeError('unexpected negative control: ' +
                                       result.stdout + result.stderr)
                print('negative control: RC7 revival retained stale image')
            elif result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            else:
                print(result.stdout, end='')


if __name__ == '__main__':
    main()
