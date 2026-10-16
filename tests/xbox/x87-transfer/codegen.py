#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile Android helper/softfloat TUs for non-LTO mechanism evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--commands', type=Path, required=True,
                        help='configured ARM64 compile_commands.json')
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    records = json.loads(args.commands.read_text())
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    provenance = []
    for source, states in (('/target/i386/tcg/fpu_helper.c', (0, 1)),
                           ('/fpu/softfloat.c', (None,))):
        record = next(r for r in records if r['file'].endswith(source))
        base = record.get('arguments') or shlex.split(record['command'])
        cleaned = []
        skip = False
        for flag in base:
            if skip:
                skip = False
                continue
            if flag in ('-o', '-MF', '-MT'):
                skip = True
            elif flag not in ('-MD', '-MMD'):
                cleaned.append(flag)
        compiler_dir = Path(cleaned[0]).parent
        objdump = compiler_dir / 'llvm-objdump'
        nm = compiler_dir / 'llvm-nm'
        for enabled in states:
            label = ('softfloat' if enabled is None else
                     'transfer-' + str(enabled))
            obj = out / (label + '.o')
            command = cleaned + ['-fno-lto', '-UXEMU_ENABLE_CPU_PROFILE',
                                 '-DXEMU_ENABLE_CPU_PROFILE=0']
            if enabled is not None:
                command += ['-UXEMU_OPT_X87_TRANSFER',
                            '-DXEMU_OPT_X87_TRANSFER=' + str(enabled)]
            command += ['-o', str(obj)]
            subprocess.run(command, cwd=record['directory'], check=True)
            for tool, flags, suffix in (
                    (objdump, ['-d', '--no-show-raw-insn'], '.asm'),
                    (nm, ['-S', '--size-sort'], '.sizes')):
                result = subprocess.check_output([str(tool)] + flags +
                                                 [str(obj)], text=True)
                (out / (label + suffix)).write_text(result)
            source_sha = hashlib.sha256(
                Path(record['file']).read_bytes()).hexdigest()
            provenance.append({'label': label, 'command': command,
                               'directory': record['directory'],
                               'source_sha256': source_sha})
            print(label + ': actual ARM64 TU compiled; non-LTO profile OFF')
    (out / 'commands.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print('Instruction paths/calls are mechanism evidence; final APK ThinLTO '
          'may inline differently. No device timing is measured here.')


if __name__ == '__main__':
    main()
