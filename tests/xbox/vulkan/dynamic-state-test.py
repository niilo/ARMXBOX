#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare effective dynamic Vulkan arguments against immutable RC8 source."""

import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

BASELINE = 'beb317063ded5b2a198ef044201682de0562ced2'


def dynamic_body(source):
    start = source.index('    if (!pg->clearing) {\n#if OPT_DYNAMIC_STATES',
                         source.index('static void begin_draw('))
    end = source.index('        if (!r->pre_draw_skipped)', start)
    # The original clearing guard closes after unrelated descriptor setup.
    return source[start:end] + '\n}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--baseline-ref', default=BASELINE)
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    relative = 'hw/xbox/nv2a/pgraph/vk/draw.c'
    source = (repo / relative).read_text()
    original = subprocess.check_output(
        ['git', 'show', args.baseline_ref + ':' + relative], cwd=repo,
        text=True)
    current = dynamic_body(source)
    baseline = dynamic_body(original)
    # Remove just the declared dependency; retain its use in command args.
    negative, removed = re.subn(
        r'(const uint32_t depth_mask\s*=.*?NV_PGRAPH_CONTROL_0_ZFUNC)\s*'
        r'\|\s*NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE;',
        r'\1;', current, count=1, flags=re.S)
    if removed != 1:
        raise RuntimeError('cannot create missing-dependency negative control')
    constants = (repo / 'hw/xbox/nv2a/pgraph/vk/constants.h').read_text()
    names = ('cull_face', 'depth_func', 'stencil_func', 'stencil_op',
             'blend_factor', 'blend_equation')
    arrays = '\n'.join(re.search(
        r'static const \w+ pgraph_' + name + r'_vk_map\[\] = \{.*?\n\};',
        constants, re.S).group() for name in names)
    tokens = sorted(set(re.findall(r'\bVK_[A-Z0-9_]+\b', arrays + current)))
    supplied = {'VK_TRUE', 'VK_FALSE', 'VK_STENCIL_FACE_FRONT_AND_BACK',
                'VK_COLOR_COMPONENT_R_BIT', 'VK_COLOR_COMPONENT_G_BIT',
                'VK_COLOR_COMPONENT_B_BIT', 'VK_COLOR_COMPONENT_A_BIT'}
    # Enum identities stand in for the Vulkan header; map tables and argument
    # calculations are actual production code in both compared versions.
    defines = '\n'.join('#define ' + name + ' ' + str(i + 10)
                        for i, name in enumerate(tokens)
                        if name not in supplied)
    functions = defines + '\n' + arrays + '\n'
    for name, body in (('old_update', baseline), ('new_update', current),
                       ('negative_update', negative)):
        functions += ('static void ' + name +
                      '(PGRAPHState *pg, PGRAPHVkState *r)\n{\n' + body +
                      '\n}\n')
    header = (repo / 'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
    start = header.index('#if OPT_DYNAMIC_STATES\n    struct {')
    end = header.index('} dyn_state;', start) + len('} dyn_state;')
    types = header[start:end] + '\n#endif\n'
    with tempfile.TemporaryDirectory(prefix='dynamic-state-') as tmp:
        out = args.output_dir.resolve() if args.output_dir else Path(tmp)
        out.mkdir(parents=True, exist_ok=True)
        (out / 'dynamic-state-functions.inc').write_text(functions)
        (out / 'dynamic-state-types.inc').write_text(types)
        for blend in (0, 1):
            binary = out / ('fixture-' + str(blend))
            command = shlex.split(args.cc) + [
                '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-const-variable', '-UNDEBUG',
                '-DOPT_DYNAMIC_STATES=1', '-DOPT_DYNAMIC_BLEND=' + str(blend),
                '-I' + str(repo), '-I' + str(out),
                str(here / 'dynamic-state-test.c'), '-o', str(binary)]
            if args.sanitize:
                command += ['-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer']
            subprocess.run(command, check=True)
            result = subprocess.run([str(binary)], capture_output=True,
                                    text=True, check=True)
            (out / ('fixture-' + str(blend) + '.log')).write_text(
                result.stdout + result.stderr)
            print('OPT_DYNAMIC_BLEND=' + str(blend))
            print(result.stdout, end='')
            if args.negative_control:
                result = subprocess.run([str(binary), 'baseline-control'],
                                        capture_output=True, text=True)
                if (result.returncode != 1 or
                        'redundant dynamic command survived' not in
                        result.stderr):
                    raise RuntimeError('unexpected baseline control outcome: ' +
                                       result.stdout + result.stderr)
                print('RC8 operation-count negative control: detected')


if __name__ == '__main__':
    main()
