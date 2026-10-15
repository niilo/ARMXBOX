#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the retained synthetic DSP basic program with C and Rust JIT engines."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', default='build')
    args = parser.parse_args()
    repo = Path(subprocess.check_output(
        ['git', 'rev-parse', '--show-toplevel'], text=True).strip())
    build = Path(args.build_dir).resolve()
    output = build / 'cloud-audio' / 'dsp-engines'
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ,
               G_TEST_SRCDIR=str(repo / 'tests/xbox/dsp'),
               G_TEST_BUILDDIR=str(build / 'tests/xbox/dsp'))
    subprocess.run([str(build / 'tests/xbox/dsp/test-xbox-mcpx-dsp'),
                    '--tap', '-k'], cwd=build, env=env, check=True)
    source = (repo / 'tests/xbox/dsp/test-dsp.c').read_text()
    anchor = '    g_test_init(&argc, &argv, NULL);'
    assert source.count(anchor) == 1
    source = source.replace(anchor,
                            '    g_config.audio.use_dsp_jit = true;\n' + anchor)
    test = output / 'test-dsp-jit.c'
    test.write_text(source)
    commands = json.loads((build / 'compile_commands.json').read_text())
    entry = next(x for x in commands
                 if x['file'].endswith('/dsp/test-dsp.c'))
    compile_cmd = shlex.split(entry['command'])
    original_object = compile_cmd[compile_cmd.index('-o') + 1]
    compile_cmd[compile_cmd.index('-o') + 1] = str(output / 'test-dsp-jit.o')
    compile_cmd[compile_cmd.index('-c') + 1] = str(test)
    subprocess.run(compile_cmd, cwd=build, check=True)
    target = 'tests/xbox/dsp/test-xbox-mcpx-dsp'
    native_commands = subprocess.check_output(
        ['ninja', '-t', 'commands', target], cwd=build, text=True).splitlines()
    link = shlex.split(native_commands[-1])
    link[link.index('-o') + 1] = str(output / 'test-dsp-jit')
    link = [str(output / 'test-dsp-jit.o') if x == original_object else x
            for x in link]
    print('Native DSP link inputs:', flush=True)
    for arg in link:
        if ('apu/dsp/' in arg or 'dsp56300' in arg or
                arg.endswith('libqemuutil.a')):
            print(' ', arg, flush=True)
    subprocess.run(link, cwd=build, check=True)
    subprocess.run([str(output / 'test-dsp-jit'), '--tap', '-k'],
                   cwd=build, env=env, check=True)
    print('PASS: actual native C interpreter and Rust DSP JIT basic program; '
          'only the external fixture preference differs')


if __name__ == '__main__':
    main()
