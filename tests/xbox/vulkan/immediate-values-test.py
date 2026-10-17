#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise real setters, binders, push values and pipeline dispatch."""
import argparse
from pathlib import Path
import re
import runpy
import shlex
import subprocess
import tempfile


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
    cache = runpy.run_path(str(here / 'vertex-cache-test.py'))
    primitive = runpy.run_path(str(here / 'primitive-cache-test.py'))
    function, definition = cache['function'], cache['definition']
    braced = primitive['braced']
    prefix = 'hw/xbox/nv2a/pgraph/'

    def run(original):
        def read(path):
            if original:
                return subprocess.check_output(
                    ['git', 'show', args.baseline_ref + ':' + path],
                    cwd=repo, text=True)
            return (repo / path).read_text()
        vertex = read(prefix + 'vk/vertex.c')
        draw = read(prefix + 'vk/draw.c')
        common = read(prefix + 'vertex.c')
        shaders = read(prefix + 'glsl/shaders.c')
        methods = read(prefix + 'pgraph.c')
        regs = read('hw/xbox/nv2a/nv2a_regs.h')
        enums = read(prefix + 'vsh_regs.h')
        arrays = [definition(vertex, r'^static const VkFormat ' + name + r'\[')
                  for name in ('float_to_count', 'ub_to_count', 's1_to_count',
                               's32k_to_count', 's32k_int_to_count')]
        formats = sorted(set(re.findall(r'VK_FORMAT_\w+', ''.join(arrays))))
        formats.append('VK_FORMAT_R32_SINT')
        binder = ['enum { ' + ', '.join(formats) + ' };'] + arrays
        binder += [function(common, 'pgraph_update_inline_value'),
                   function(common, 'pgraph_get_inline_values'),
                   function(vertex, 'update_memory_buffer')]
        for name in ('update_inline_attribute_value',
                     'update_immediate_attribute_values'):
            if 'static void ' + name + '(' in vertex:
                binder.append(function(vertex, name))
        binder.append(function(vertex, 'pgraph_vk_bind_vertex_attributes'))
        start = enums.index('enum ShaderPrimitiveMode {')
        pieces = [braced(enums, start) + ';',
                  function(vertex, 'pgraph_vk_bind_vertex_attributes_inline'),
                  function(common, 'pgraph_allocate_inline_buffer_vertices'),
                  function(draw, 'draw_constants_dirty'),
                  function(draw, 'push_vertex_attr_values')]
        if 'bool pgraph_glsl_vertex_attrs_match(' in shaders:
            pieces.append(function(shaders, 'pgraph_glsl_vertex_attrs_match'))
        if 'static bool vertex_input_matches_pipeline(' in draw:
            pieces.append('#define HAVE_VERTEX_INPUT_MATCHES')
            pieces.append(function(draw, 'vertex_input_matches_pipeline'))
        names = ('SET_VERTEX_DATA2F_M', 'SET_VERTEX_DATA4F_M',
                 'SET_VERTEX_DATA2S', 'SET_VERTEX_DATA4UB',
                 'SET_VERTEX_DATA4S_M', 'SET_DIFFUSE_COLOR4F')
        macro = re.search(r'^#define SET_VERTEX_ATTRIBUTE_F[^\n]*', methods,
                          re.MULTILINE)
        assert macro
        end = macro.end()
        while methods[macro.start():end].rstrip().endswith('\\'):
            end = methods.index('\n', end + 1)
        pieces.append(methods[macro.start():end])
        for name in names:
            constant = re.search(r'^#\s*define NV097_' + name +
                                 r'\s+0x[0-9A-Fa-f]+', regs, re.MULTILINE)
            assert constant
            pieces.append(constant.group())
            start = methods.index('DEF_METHOD_INC(NV097, ' + name + ')')
            body = braced(methods, start)
            body = body[body.index('{'):]
            pieces.append('static void set_' + name + '(PGRAPHState *pg, '
                          'unsigned method, uint32_t parameter)\n' + body)
        start = draw.index('else if (r->uniforms_changed)',
                           draw.index('static void begin_pre_draw('))
        clause = braced(draw, start)[len('else '):]
        pieces.append('static bool sfp_uniform_guard(PGRAPHVkState *r)\n{\n'
                      'bool sfp_ok = true;\n' + clause +
                      '\nreturn sfp_ok;\n}')
        start = draw.index('if (r->pipeline_binding &&',
                           draw.index('static void create_pipeline('))
        block = braced(draw, start)
        pieces.append('static void run_pipeline_early(PGRAPHState *pg)\n{\n'
                      'PGRAPHVkState *r = pg->vk_renderer_state;\n' + block +
                      '\nassert(!"expected a pipeline cache hit");\n}')
        for label, marker in (
                ('merged', '/* Replayed draws replaced GPU constants'),
                ('reorder', '/* Captured push constants need not match')):
            body = ''
            if marker in draw:
                start = draw.index('r->immediate_attrs_valid = 0;',
                                   draw.index(marker))
                end = draw.index('r->uniforms_changed = true;', start)
                end += len('r->uniforms_changed = true;')
                body = draw[start:end]
            pieces.append('static void invalidate_' + label + '_values('
                          'PGRAPHVkState *r)\n{\n(void)r;\n' + body + '\n}')
        with tempfile.TemporaryDirectory(prefix='immediate-values-') as tmp:
            out = Path(tmp)
            (out / 'vertex-cache-functions.inc').write_text(
                '\n\n'.join(binder) + '\n')
            (out / 'immediate-values-functions.inc').write_text(
                '\n\n'.join(pieces) + '\n')
            binary = out / 'fixture'
            command = shlex.split(args.cc) + [
                '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-UNDEBUG',
                '-Wno-sign-compare', '-Wno-unused-parameter',
                '-fno-strict-aliasing', '-I' + tmp,
                str(here / 'immediate-values-test.c'), '-o', str(binary)]
            if args.sanitize:
                command += ['-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer']
            subprocess.run(command, cwd=repo, check=True)
            result = subprocess.run([str(binary)], capture_output=True,
                                    text=True)
            if original:
                expected = ('immediate setter change escaped the SFP '
                            'uniform guard')
                if result.returncode == 0 or expected not in result.stderr:
                    raise RuntimeError('unexpected negative control: ' +
                                       result.stdout + result.stderr)
                print('negative control: original binder permits stale '
                      'immediate constants')
            else:
                if result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                print(result.stdout, end='')

    run(False)
    if args.negative_control:
        run(True)


if __name__ == '__main__':
    main()
