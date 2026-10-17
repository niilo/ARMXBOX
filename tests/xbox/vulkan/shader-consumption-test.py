#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise shader-change production, draw consumption and cache decisions."""
import argparse
from pathlib import Path
import re
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
    parser.add_argument('--baseline-ref', default=BASELINE)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    helpers = runpy.run_path(str(here / 'primitive-cache-test.py'))
    braced, function = helpers['braced'], helpers['function']
    prefix = 'hw/xbox/nv2a/pgraph/'
    enums = (repo / prefix / 'vsh_regs.h').read_text()
    geom_h = (repo / prefix / 'glsl/geom.h').read_text()
    types = []
    for name in ('ShaderPrimitiveMode', 'ShaderPolygonMode'):
        types.append(braced(enums, enums.index('enum ' + name + ' {')) + ';')
    start = geom_h.index('typedef struct {')
    end = geom_h.index('} GeomState;', start) + len('} GeomState;')
    types.append(geom_h[start:end])

    def run(original):
        def read(name):
            path = prefix + name
            if original:
                return subprocess.check_output(
                    ['git', 'show', args.baseline_ref + ':' + path],
                    cwd=repo, text=True)
            return (repo / path).read_text()
        draw, shaders = read('vk/draw.c'), read('vk/shaders.c')
        shared, geom = read('glsl/shaders.c'), read('glsl/geom.c')
        pieces = [function(read('prim_rewrite.c'),
                           'pgraph_prim_rewrite_get_output_mode'),
                  function(geom, 'pgraph_glsl_primitive_mode_matches'),
                  function(geom, 'pgraph_glsl_set_geom_state'),
                  function(shared, 'pgraph_glsl_vertex_attrs_match'),
                  function(shared, 'pgraph_glsl_check_shader_state_dirty'),
                  function(draw, 'draw_constants_dirty'),
                  function(draw, 'vertex_input_matches_pipeline')]
        for i, expression in enumerate(helpers['primitive_guards'](draw)):
            if i == 3:
                start = draw.index('bool sfp_ok = true;')
                end = draw.index('if (sfp_ok)', start)
                body = draw[start:end] + '\nreturn sfp_ok;'
            else:
                body = 'return ' + ('!(' if i == 2 else '(')
                body += expression + ');'
            pieces.append('static bool guard_' + str(i) +
                          '(PGRAPHState *pg)\n{\n'
                          'PGRAPHVkState *r = pg->vk_renderer_state;\n' +
                          body + '\n}')
        pieces.append(function(draw, 'check_pipeline_dirty'))
        pipeline = function(draw, 'create_pipeline')
        first_if = re.search(r'\bif\s*\(', pipeline)
        pieces.append('static void pipeline_fast_hit(PGRAPHState *pg)\n'
                      '{\nPGRAPHVkState *r = pg->vk_renderer_state;\n' +
                      braced(pipeline, first_if.start()) +
                      '\npipeline_fallbacks++;\n}')
        extra = []
        start = shaders.index('if (!r->shader_binding ||\n'
                              '            pgraph_glsl_compare_shader_state')
        extra.append('static void record_shader_change(PGRAPHState *pg, '
                     'const ShaderState *next)\n{\n'
                     'PGRAPHVkState *r = pg->vk_renderer_state;\n'
                     'ShaderState new_state = *next;\n' +
                     braced(shaders, start) + '\n}')
        start = pipeline.index('if (pg->program_data_dirty ||')
        block = braced(pipeline, start)
        end = start + len(block)
        end = pipeline.index('{', pipeline.index('else', end))
        block += ' else ' + braced(pipeline, end)
        extra.append('static void prepare_shader_uniforms(PGRAPHState *pg)\n'
                     '{\nPGRAPHVkState *r = pg->vk_renderer_state;\n' +
                     block + '\n}')
        start = shaders.index('bool tex_triggers_new_set =')
        end = shaders.index('if (need_new_descriptor_set', start)
        extra.append('static bool need_descriptor_set(PGRAPHVkState *r)\n'
                     '{\nbool push_desc = r->push_descriptors_supported;\n'
                     'int *ds_index_ptr = &r->descriptor_set_index;\n'
                     'if (push_desc) ds_index_ptr = &r->push_ubo_set_index;\n'
                     '#if OPT_BINDLESS_TEXTURES\n'
                     'if (r->bindless_textures_supported)\n'
                     '    ds_index_ptr = &r->ubo_descriptor_set_index;\n'
                     '#endif\n' + shaders[start:end] +
                     '\nreturn need_new_descriptor_set;\n}')
        begin = function(draw, 'begin_draw')
        start = begin.index('if (!pg->clearing) {')
        extra.append('static void consume_shader_state(PGRAPHState *pg)\n'
                     '{\nPGRAPHVkState *r = pg->vk_renderer_state;\n' +
                     braced(begin, start) + '\n}')
        start = begin.index('bool must_bind_pipeline =')
        end = begin.index('// Visibility testing', start)
        prefix_block = begin[start:end]
        start = begin.index('if (must_bind_pipeline)')
        end = begin.index('r->pipeline_binding_changed = false;', start)
        end += len('r->pipeline_binding_changed = false;')
        extra.append('static void bind_pipeline_prefix(PGRAPHState *pg)\n'
                     '{\nPGRAPHVkState *r = pg->vk_renderer_state;\n' +
                     prefix_block + begin[start:end] + '\n}\n}')
        extra.append('static void restore_shader_notification('
                     'PGRAPHVkState *r, bool saved_shader_bindings_changed)\n'
                     '{\n' + re.search(
                         r'r->shader_bindings_changed = '
                         r'saved_shader_bindings_changed;', draw).group() +
                     '\n}')
        start = draw.index('r->need_descriptor_rebind = true;')
        end = draw.index('r->queries_reset_in_cb = false;', start)
        end += len('r->queries_reset_in_cb = false;')
        extra.append('static void reset_command_state(PGRAPHVkState *r)\n'
                     '{\n' + draw[start:end] + '\n}')
        with tempfile.TemporaryDirectory(prefix='shader-consumption-') as tmp:
            out = Path(tmp)
            (out / 'primitive-cache-types.inc').write_text('\n'.join(types))
            (out / 'primitive-cache-functions.inc').write_text(
                '\n\n'.join(pieces))
            (out / 'shader-consumption-functions.inc').write_text(
                '\n\n'.join(extra))
            for bindless in (0, 1):
                binary = out / ('fixture-' + str(bindless))
                command = shlex.split(args.cc) + [
                    '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-sign-compare', '-UNDEBUG',
                    '-DOPT_BINDLESS_TEXTURES=' + str(bindless),
                    '-I' + str(repo), '-I' + tmp,
                    str(here / 'shader-consumption-test.c'), '-o', str(binary)]
                if args.sanitize:
                    command += ['-fsanitize=address,undefined',
                                '-fno-omit-frame-pointer']
                subprocess.run(command, check=True)
                result = subprocess.run([str(binary)], capture_output=True,
                                        text=True,
                                        preexec_fn=disable_core_dumps)
                if original:
                    expected = ('consumed shader change still blocks '
                                'repeated draws')
                    if result.returncode == 0 or expected not in result.stderr:
                        raise RuntimeError('unexpected negative control: ' +
                                           result.stdout + result.stderr)
                    print('negative control: original draw retains consumed '
                          'shader notification (bindless=' +
                          str(bindless) + ')')
                    counts = subprocess.run([str(binary), 'baseline-counts'],
                                            capture_output=True, text=True,
                                            check=True,
                                            preexec_fn=disable_core_dumps)
                    print(counts.stdout, end='')
                else:
                    if result.returncode:
                        raise RuntimeError(result.stdout + result.stderr)
                    print(result.stdout, end='')

    run(False)
    if args.negative_control:
        run(True)


if __name__ == '__main__':
    main()
