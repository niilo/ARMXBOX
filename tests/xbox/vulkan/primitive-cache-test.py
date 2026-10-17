#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check primitive cache guards against the actual geometry-state builder."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def braced(source, start):
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def function(source, name):
    match = re.search(r'\b' + name + r'\s*\([^;{}]*\)\s*\{', source)
    if not match:
        raise ValueError('missing production function: ' + name)
    start = source.rfind('\n\n', 0, match.start()) + 2
    return braced(source, start)


def primitive_guards(source):
    guards = []
    for match in re.finditer(r'\bif\s*\(', source):
        start = source.index('(', match.start()) + 1
        depth, end = 1, start
        while depth:
            depth += (source[end] == '(') - (source[end] == ')')
            end += 1
        expression = source[start:end - 1]
        if ('pgraph_glsl_primitive_mode_matches' in expression or
                re.search(r'pg->primitive_mode\s*[!=]=\s*'
                          r'r->shader_binding->state.geom.primitive_mode',
                          expression)):
            guards.append(expression)
    assert len(guards) == 6, 'changed primitive guard inventory'
    return guards


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--baseline-ref',
                        default='f75153dddd03880c0c6fbfea93b2dea8ceb29af9')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    prefix = 'hw/xbox/nv2a/pgraph/'
    geom = (repo / prefix / 'glsl/geom.c').read_text()
    geom_h = (repo / prefix / 'glsl/geom.h').read_text()
    current_shaders = (repo / prefix / 'glsl/shaders.c').read_text()
    rewrite = (repo / prefix / 'prim_rewrite.c').read_text()
    enums = (repo / prefix / 'vsh_regs.h').read_text()
    types = []
    for name in ('ShaderPrimitiveMode', 'ShaderPolygonMode'):
        types.append(braced(enums, enums.index('enum ' + name + ' {')) + ';')
    start = geom_h.index('typedef struct {')
    end = geom_h.index('} GeomState;', start) + len('} GeomState;')
    types.append(geom_h[start:end])
    for original in (False, True) if args.negative_control else (False,):
        def read(name):
            path = prefix + name
            if original:
                return subprocess.check_output(
                    ['git', 'show', args.baseline_ref + ':' + path],
                    cwd=repo, text=True)
            return (repo / path).read_text()
        draw = read('vk/draw.c')
        shaders = read('glsl/shaders.c')
        pieces = [function(rewrite, 'pgraph_prim_rewrite_get_output_mode'),
                  function(geom, 'pgraph_glsl_primitive_mode_matches'),
                  function(geom, 'pgraph_glsl_set_geom_state'),
                  function(current_shaders, 'pgraph_glsl_vertex_attrs_match'),
                  function(shaders, 'pgraph_glsl_check_shader_state_dirty'),
                  function(draw, 'draw_constants_dirty')]
        if not original:
            pieces.append(function(draw, 'vertex_input_matches_pipeline'))
        for i, expression in enumerate(primitive_guards(draw)):
            if i == 3:
                start = draw.index('bool sfp_ok = true;')
                end = draw.index('if (sfp_ok)', start)
                body = draw[start:end] + '\nreturn sfp_ok;'
            else:
                # The third check requests a bind when state does not match.
                body = 'return ' + ('!(' if i == 2 else '(')
                body += expression + ');'
            pieces.append('static bool guard_' + str(i) +
                          '(PGRAPHState *pg)\n{\n'
                          'PGRAPHVkState *r = pg->vk_renderer_state;\n' +
                          body + '\n}')
        if not original:
            pieces.append(function(draw, 'check_pipeline_dirty'))
            pipeline = function(draw, 'create_pipeline')
            first_if = re.search(r'\bif\s*\(', pipeline)
            pieces.append('static void pipeline_fast_hit(PGRAPHState *pg)\n'
                          '{\nPGRAPHVkState *r = pg->vk_renderer_state;\n' +
                          braced(pipeline, first_if.start()) +
                          '\npipeline_fallbacks++;\n}')
        with tempfile.TemporaryDirectory(prefix='primitive-cache-') as tmp:
            out = Path(tmp)
            (out / 'primitive-cache-types.inc').write_text('\n'.join(types))
            (out / 'primitive-cache-functions.inc').write_text(
                '\n\n'.join(pieces))
            for bindless in (0, 1):
                binary = out / ('fixture-' + str(bindless))
                command = shlex.split(args.cc) + [
                    '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-sign-compare', '-UNDEBUG',
                    '-DOPT_BINDLESS_TEXTURES=' + str(bindless),
                    '-I' + str(repo), '-I' + str(out),
                    str(here / 'primitive-cache-test.c'), '-o', str(binary)]
                if original:
                    command += ['-DNEGATIVE_CONTROL']
                if args.sanitize:
                    command += ['-fsanitize=address,undefined',
                                '-fno-omit-frame-pointer']
                subprocess.run(command, check=True)
                result = subprocess.run([str(binary)], check=True,
                                        text=True, capture_output=True)
                print(result.stdout, end='')


if __name__ == '__main__':
    main()
