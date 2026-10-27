#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual GPU shader faults must fail the independent readback oracle."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('executable', type=Path)
parser.add_argument('shaders', type=Path)
args = parser.parse_args()
source = Path(__file__).parent
variants = {
    'channel-swizzle': ('frag', (source / 'output.frag').read_text().replace(
        'texture(checker, uv);', 'texture(checker, uv).bgra;')),
    'ignored-vertices': ('vert', '''#version 450
layout(location=0) out vec2 uv;
void main() {
    vec2 p = vec2[3](vec2(-1,-1),vec2(-1,1),vec2(1,-1))[gl_VertexIndex];
    gl_Position = vec4(p,0,1);
    uv = (p+1)*0.5;
}
'''),
}
for name, (stage, text) in variants.items():
    with tempfile.TemporaryDirectory(prefix='armxbox-vulkan-negative-') as temp:
        root = Path(temp)
        for suffix in ('vert', 'frag'):
            shutil.copyfile(args.shaders / f'output.{suffix}.spv', root / f'output.{suffix}.spv')
        shader = root / f'wrong.{stage}'
        shader.write_text(text)
        subprocess.run(['glslangValidator', '--target-env', 'vulkan1.0', '-V', str(shader),
                        '-o', str(root / f'output.{stage}.spv')], check=True, capture_output=True)
        subprocess.run(['spirv-val', '--target-env', 'vulkan1.0',
                        str(root / f'output.{stage}.spv')], check=True)
        result = subprocess.run([str(args.executable.resolve()), str(root)],
                                capture_output=True, text=True, timeout=30)
        if result.returncode != 1 or 'FAIL offscreen consumer: pixel oracle mismatch' not in result.stderr:
            raise RuntimeError(f'{name} was not rejected: {result.returncode}\n{result.stdout}\n{result.stderr}')
        print(f'PASS executable GPU negative control: {name} rejected by pixel oracle')
