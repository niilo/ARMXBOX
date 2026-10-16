#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute actual x87 stack emitter IR against immutable RC10 helper bodies."""

import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

BASELINE = 'fa8b312c49292e60c53f5f8221a33d4223e282bb'
NAMES = ('fpush', 'fpop', 'fmov_FT0_STN', 'fmov_ST0_STN',
         'fmov_STN_ST0', 'fxchg_ST0_STN')


def function(source, name):
    match = re.search(r'^(?:static\s+)?(?:inline\s+)?[A-Za-z_]\w*\s+' +
                      name + r'\s*\([^;{}]*\)\s*\{', source, re.M)
    if not match:
        raise RuntimeError('function not found: ' + name)
    start = source.rfind('\n', 0, match.start()) + 1
    pos = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]


def generate(repo, original, candidate):
    helpers = subprocess.check_output(
        ['git', 'show', original + ':target/i386/tcg/fpu_helper.c'],
        cwd=repo, text=True)
    helper_names = (('fpush', 'fpop') + tuple('helper_' + n for n in NAMES) +
                    ('helper_enter_mmx', 'helper_emms', 'helper_fchs_ST0'))
    softfloat = (repo / 'include/fpu/softfloat.h').read_text()
    reference = function(softfloat, 'floatx80_chs') + '\n\n'
    reference += '\n\n'.join(function(helpers, n) for n in helper_names)
    reference = reference.replace('#if USE_NATIVE_DOUBLE_STORAGE', '#if 0')
    parts = [function(candidate, name) for name in
             ('gen_stn_ptr', 'gen_ft0_ptr', 'gen_set_fptag', 'gen_flush_fip',
              'gen_flush_fp', 'use_raw_x87_stack', 'gen_x87_raw_move')]
    parts += [function(candidate, 'gen_' + n) for n in NAMES]
    macros = candidate[candidate.index('#define GEN_HELPER_FALLBACK_v_v'):
                       candidate.index('static bool use_raw_x87_stack')]
    body = macros + '\n\n'.join(parts)
    return reference, body


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
    current = (repo / 'target/i386/tcg/translate.c').read_text()
    reference, body = generate(repo, args.baseline_ref, current)
    # The layout is copied from the actual ARM64 definition, not host long
    # double; the host fixture executes integer operations only.
    types = (repo / 'include/fpu/softfloat-types.h').read_text()
    arm = re.search(r'#else\ntypedef struct \{\n    uint64_t low;\n'
                    r'    uint16_t high;\n\} floatx80;', types).group()
    arm = arm[len('#else\n'):]
    cpu = (repo / 'target/i386/cpu.h').read_text()
    fpreg = re.search(r'typedef union \{\n    floatx80 d .*?\n\} FPReg;',
                      cpu, re.S).group()
    variants = [('normal', body, '', 1), ('host-guard', body, '', 0)]
    if args.negative_control:
        missing_flush = body.replace('        gen_flush_fp(s);', '', 1)
        lost_high = body.replace('tcg_gen_st_i64(hi, dst, 8);', '')
        if missing_flush == body or lost_high == body:
            raise RuntimeError('negative-control source boundary changed')
        old_source = subprocess.check_output(
            ['git', 'show', args.baseline_ref + ':target/i386/tcg/translate.c'],
            cwd=repo, text=True)
        old_emitters = body
        for name in NAMES:
            old_emitters = old_emitters.replace(
                function(current, 'gen_' + name),
                function(old_source, 'gen_' + name))
        variants += [('missing-flush', missing_flush, 'state mismatch', 1),
                     ('lost-high', lost_high, 'state mismatch', 1),
                     ('old-source', old_emitters, 'old helper call remains', 1)]
    with tempfile.TemporaryDirectory(prefix='x87-stack-') as tmp:
        out = args.output_dir.resolve() if args.output_dir else Path(tmp)
        out.mkdir(parents=True, exist_ok=True)
        (out / 'types.inc').write_text(arm + '\n' + fpreg)
        (out / 'reference.inc').write_text(reference)
        for label, code, expected, arm64 in variants:
            (out / 'emitters.inc').write_text(code)
            switches = (0, 1) if not expected and arm64 else (1,)
            for enabled in switches:
                name = label + '-' + str(enabled)
                binary = out / name
                command = shlex.split(args.cc) + [
                    '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                    '-UNDEBUG', '-DXEMU_OPT_X87_STACK=' + str(enabled),
                    '-DFIXTURE_ARM64=' + str(arm64),
                    '-I' + str(out), str(here / 'test.c'), '-o', str(binary)]
                if label == 'old-source':
                    command += ['-Wno-unused-function']
                if args.sanitize:
                    command += ['-fsanitize=address,undefined',
                                '-fno-omit-frame-pointer']
                subprocess.run(command, check=True)
                result = subprocess.run([str(binary)], text=True,
                                        capture_output=True)
                (out / (name + '.log')).write_text(
                    result.stdout + result.stderr)
                if expected:
                    if result.returncode != 1 or expected not in result.stderr:
                        raise RuntimeError('unexpected ' + name + ': ' +
                                           result.stdout + result.stderr)
                    print('negative control ' + name + ': detected')
                else:
                    if result.returncode:
                        raise RuntimeError(result.stdout + result.stderr)
                    print('XEMU_OPT_X87_STACK=' + str(enabled) +
                          ', ARM64=' + str(arm64) + ', guest TL=32')
                    print(result.stdout, end='')
        # Retained output should show the normal candidate, not the last mutant.
        (out / 'emitters.inc').write_text(body)


if __name__ == '__main__':
    main()
