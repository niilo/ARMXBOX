#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the snapshot equivalence test using an existing native build config."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--thinlto', action='store_true')
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[3]
    build = (args.build_dir or repo / 'build').resolve()
    out = build / 'cloud-vulkan'
    out.mkdir(parents=True, exist_ok=True)
    commands = json.loads((build / 'compile_commands.json').read_text())
    entry = next(c for c in commands
                 if c['file'].endswith('pgraph/vk/render_thread.c'))
    compiler_args = []
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
        compiler_args.append(arg)
    compiler_args += ['-O2', '-g0', '-ffunction-sections', '-fdata-sections']
    lto_args = ['-flto=thin'] if args.thinlto else []
    name = 'thinlto' if args.thinlto else 'separate-tu'
    objects = []
    sources = (repo / 'hw/xbox/nv2a/pgraph/vk/render_thread.c',
               Path(__file__).with_name('snapshot-guard.c'))
    for source in sources:
        obj = out / (source.stem + '-' + name + '.o')
        subprocess.run(compiler_args + lto_args +
                       ['-c', str(source), '-o', str(obj)],
                       cwd=entry['directory'], check=True)
        objects.append(str(obj))
    binary = out / ('snapshot-' + name)
    subprocess.run([compiler_args[0], '-fuse-ld=lld'] + lto_args + objects +
                   ['-Wl,--gc-sections', '-lglib-2.0', '-pthread',
                    '-o', str(binary)], check=True)
    command = [str(binary)] + (['--benchmark'] if args.benchmark else [])
    result = subprocess.run(command, check=True, stdout=subprocess.PIPE,
                            text=True)
    (out / (name + '.log')).write_text(result.stdout)
    print(result.stdout, end='')


if __name__ == '__main__':
    main()
