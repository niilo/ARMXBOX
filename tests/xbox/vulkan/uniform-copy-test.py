#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare real uniform packing/copying with the immutable pre-fix source."""
import argparse
from pathlib import Path
import resource
import runpy
import shlex
import subprocess
import tempfile

BASELINE = '8aa78b9cb878c8cc9f11388ec28bad1d6a3ef086'


def disable_core_dumps():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--baseline-ref', default=BASELINE)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    helper = runpy.run_path(str(here / 'primitive-cache-test.py'))
    function, braced = helper['function'], helper['braced']
    path = 'hw/xbox/nv2a/pgraph/vk/glsl.h'
    header = (repo / path).read_text()
    original = subprocess.check_output(
        ['git', 'show', args.baseline_ref + ':' + path], cwd=repo, text=True)
    types = []
    for name in ('ShaderUniform', 'ShaderUniformLayout'):
        types.append(braced(header, header.index('typedef struct ' + name)) +
                     ' ' + name + ';')
    old_copy = function(original, 'uniform_copy').replace(
        'void uniform_copy(', 'void uniform_copy_original(', 1)
    reflection = (repo / 'hw/xbox/nv2a/pgraph/vk/glsl.c').read_text()
    common = types + [function(header, 'uniform_std140'),
                      function(header, 'uniform_std430'),
                      function(header, 'uniform_ptr'), old_copy,
                      function(reflection, 'block_to_uniforms')]

    def run(negative=False, benchmark=False):
        with tempfile.TemporaryDirectory(prefix='uniform-copy-') as tmp:
            out = Path(tmp)
            copy_source = original if negative else header
            (out / 'uniform-copy-functions.inc').write_text(
                '\n\n'.join(common + [function(copy_source, 'uniform_copy')]))
            binary = out / 'fixture'
            command = shlex.split(args.cc) + [
                '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                '-Wno-sign-compare', '-UNDEBUG', '-I' + tmp,
                str(here / 'uniform-copy-test.c'), '-o', str(binary)]
            if benchmark:
                command += ['-DUNIFORM_COPY_BENCHMARK', '-Wno-unused-function']
            elif args.sanitize:
                command += ['-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer']
            subprocess.run(command, check=True)
            result = subprocess.run([str(binary)], capture_output=True,
                                    text=True, preexec_fn=disable_core_dumps)
            if negative:
                expected = 'contiguous uniform still uses per-element copies'
                if result.returncode == 0 or expected not in result.stderr:
                    raise RuntimeError('unexpected operation control: ' +
                                       result.stdout + result.stderr)
                print('negative control: original copy retains per-element '
                      'memcpy operations')
            else:
                if result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                print(result.stdout, end='')
            if not negative and not benchmark:
                for kind in ('partial-vector', 'too-many', 'bounds'):
                    result = subprocess.run([str(binary), kind],
                                            capture_output=True, text=True,
                                            preexec_fn=disable_core_dumps)
                    if (result.returncode == 0 or
                            'Assertion' not in result.stderr):
                        raise RuntimeError('missing assertion: ' + kind +
                                           result.stdout + result.stderr)
                print('uniform copy: whole-vector/count/allocation assertions '
                      'reject invalid inputs')

    run()
    if args.negative_control:
        run(negative=True)
    if args.benchmark:
        run(benchmark=True)


if __name__ == '__main__':
    main()
