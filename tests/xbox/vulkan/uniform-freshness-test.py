#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise uniform setters/getters and actual Vulkan cache dispatch guards."""
import argparse
from pathlib import Path
import re
import resource
import runpy
import shlex
import subprocess
import tempfile

BASELINE = 'd2da241bab37c6460fc8832e8ded75d42362e739'


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-control', action='store_true')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    helper = runpy.run_path(str(here / 'primitive-cache-test.py'))
    function, braced = helper['function'], helper['braced']
    prefix = 'hw/xbox/nv2a/pgraph/'
    enums = (repo / prefix / 'vsh_regs.h').read_text()
    geom_h = (repo / prefix / 'glsl/geom.h').read_text()
    types = [braced(enums, enums.index('enum ' + n + ' {')) + ';'
             for n in ('ShaderPrimitiveMode', 'ShaderPolygonMode')]
    start = geom_h.index('typedef struct {')
    types.append(geom_h[start:geom_h.index('} GeomState;', start) +
                        len('} GeomState;')])

    def run(original):
        def read(name):
            path = prefix + name
            if original:
                return subprocess.check_output(
                    ['git', 'show', BASELINE + ':' + path], cwd=repo, text=True)
            return (repo / path).read_text()
        draw, methods = read('vk/draw.c'), read('pgraph.c')
        pieces = [function(read('prim_rewrite.c'),
                           'pgraph_prim_rewrite_get_output_mode'),
                  function(read('glsl/geom.c'),
                           'pgraph_glsl_primitive_mode_matches'),
                  function(read('glsl/geom.c'), 'pgraph_glsl_set_geom_state'),
                  function(read('glsl/shaders.c'),
                           'pgraph_glsl_vertex_attrs_match'),
                  function(read('glsl/shaders.c'),
                           'pgraph_glsl_check_shader_state_dirty'),
                  function(draw, 'draw_constants_dirty'),
                  function(draw, 'vertex_input_matches_pipeline')]
        for i, expression in enumerate(helper['primitive_guards'](draw)):
            if i == 3:
                start = draw.index('bool sfp_ok = true;')
                body = draw[start:draw.index('if (sfp_ok)', start)]
                body += '\nreturn sfp_ok;'
            else:
                body = 'return ' + ('!(' if i == 2 else '(')
                body += expression + ');'
            pieces.append('static bool guard_' + str(i) +
                          '(PGRAPHState *pg)\n{\n'
                          'PGRAPHVkState *r = pg->vk_renderer_state;\n' +
                          body + '\n}')
        pieces.append(function(draw, 'check_pipeline_dirty'))
        pipeline = function(draw, 'create_pipeline')
        start = re.search(r'\bif\s*\(', pipeline).start()
        pieces.append('static void pipeline_fast_hit(PGRAPHState *pg)\n{\n'
                      'PGRAPHVkState *r = pg->vk_renderer_state;\n' +
                      braced(pipeline, start) + '\npipeline_fallbacks++;\n}')
        extra = [function(methods, 'pgraph_init_reg_dynamic_masks'),
                 function(methods, 'pgraph_init_reg_category_table'),
                 '#define regs_ regs\n' +
                 function(read('pgraph.h'), 'pgraph_reg_w') + '\n#undef regs_']
        for name in ('SET_MATERIAL_ALPHA', 'SET_LIGHT_AMBIENT_COLOR',
                     'SET_ALPHA_REF', 'SET_COMBINER_FACTOR0'):
            match = re.search(r'DEF_METHOD(?:_INC)?\(NV097, ' + name + r'\)',
                              methods)
            block = braced(methods, match.start())
            extra.append('static void set_' + name + '(PGRAPHState *pg, '
                         'unsigned method, uint32_t parameter)\n' +
                         block[block.index('{'):])
        header = read('pgraph.h')
        extra[:0] = [line for line in header.splitlines()
                     if line.startswith('#define REG_CAT_')]
        extra.insert(3, '#define PG_SET_MASK(reg, mask, value) do { '
                     'uint32_t rv = pgraph_reg_r(pg, reg); '
                     'SET_MASK(rv, mask, value); '
                     'pgraph_reg_w(pg, reg, rv); } while (0)')
        getter = function(read('glsl/vsh.c'),
                          'pgraph_glsl_set_vsh_uniform_values')
        getters = []
        for field in ('material_alpha', 'lightInfiniteHalfVector',
                      'lightInfiniteDirection', 'lightLocalPosition',
                      'lightLocalAttenuation'):
            start = getter.index('if (locs[VshUniform_' + field + ']')
            getters.append(braced(getter, start))
        extra.append('static void get_lighting(PGRAPHState *pg, '
                     'VshUniformValues *values)\n{\n'
                     'int locs[5] = {0};\n' + '\n'.join(getters) + '\n}')
        psh = function(read('glsl/psh.c'),
                       'pgraph_glsl_set_psh_uniform_values')
        start = psh.index('if (locs[PshUniform_alphaRef]')
        extra.append('static void get_alpha(PGRAPHState *pg, '
                     'PshUniformValues *values)\n{\nint locs[1] = {0};\n' +
                     braced(psh, start) + '\n}')
        start = psh.index('bool polygon_offset_enabled = false;')
        end = psh.index('if (locs[PshUniform_surfaceScale]', start)
        extra.append('static void get_depth(PGRAPHState *pg, '
                     'PshUniformValues *values)\n{\nint locs[3] = {0};\n' +
                     psh[start:end] + '\n}')
        shader = read('vk/shaders.c')
        flags = re.findall(r'pg->(?:vsh_constants|ltctxa|ltctxb|ltc1)'
                           r'_any_dirty '
                           r'= false;', shader)
        assert len(flags) == 4
        primitive_record = re.search(
            r'r->uniforms_primitive_is_polygon\s*=\s*'
            r'pg->primitive_mode >= PRIM_TYPE_TRIANGLES;', shader)
        record = primitive_record.group() if primitive_record else ''
        extra.append('static void consume_constant_flags(PGRAPHState *pg)\n'
                     '{\nPGRAPHVkState *r = pg->vk_renderer_state;\n' +
                     '\n'.join(flags) + '\n' + record + '\n}')
        base = (here / 'primitive-cache-test.c').read_text()
        # Preserve actual register writer indexing in this fixture's storage.
        base = base.replace('pg->regs[reg / 4]', 'pg->regs[reg]')
        base = base.replace('pg.regs[NV_PGRAPH_SETUPRASTER / 4]',
                            'pg.regs[NV_PGRAPH_SETUPRASTER]')
        fields = '''
    float material_alpha;
    float light_infinite_half_vector[8][3], light_infinite_direction[8][3];
    float light_local_position[8][3], light_local_attenuation[8][3];
    uint32_t ltctxa[26][4], ltctxb[52][4], ltc1[20][4];
    bool ltctxa_dirty[26], ltctxb_dirty[52], ltc1_dirty[20];
    uint64_t regs_dirty[128];
'''
        base = base.replace('} PGRAPHState;', fields + '} PGRAPHState;')
        base = base.replace('static void pgraph_vk_update_shader_uniforms(',
                            'static float actual_live_uniform(PGRAPHState *);\n'
                            'static void consume_constant_flags(PGRAPHState *);'
                            '\nstatic void pgraph_vk_update_shader_uniforms(')
        base = base.replace('allocation_uniform = live_uniform;',
                            'allocation_uniform = actual_live_uniform(state);'
                            '\n    consume_constant_flags(state);')
        with tempfile.TemporaryDirectory(prefix='uniform-freshness-') as tmp:
            out = Path(tmp)
            (out / 'primitive-cache-types.inc').write_text('\n'.join(types))
            (out / 'primitive-cache-functions.inc').write_text(
                '\n\n'.join(pieces))
            (out / 'uniform-freshness-base.inc').write_text(base)
            (out / 'uniform-freshness-functions.inc').write_text(
                '\n\n'.join(extra))
            for bindless in (0, 1):
                binary = out / ('fixture-' + str(bindless))
                command = shlex.split(args.cc) + [
                    '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-sign-compare', '-Wno-unused-parameter',
                    '-Wno-unused-variable', '-fno-strict-aliasing', '-UNDEBUG',
                    '-DOPT_BINDLESS_TEXTURES=' + str(bindless),
                    '-I' + str(repo), '-I' + tmp,
                    str(here / 'uniform-freshness-test.c'), '-o', str(binary)]
                if args.sanitize:
                    command += ['-fsanitize=address,undefined',
                                '-fno-omit-frame-pointer']
                subprocess.run(command, check=True)
                cases = ('nonregister', 'register', 'primitive') if original \
                        else ('all',)
                for case in cases:
                    result = subprocess.run([str(binary), case],
                                            capture_output=True, text=True,
                                            preexec_fn=no_core)
                    if original:
                        expected = {
                            'nonregister': 'nonregister value escaped SFP',
                            'register': 'register uniform stayed stale',
                            'primitive': 'primitive offset escaped SFP',
                        }[case]
                        if (result.returncode == 0 or
                                expected not in result.stderr):
                            raise RuntimeError('unexpected RC7 control: ' +
                                               result.stdout + result.stderr)
                        print('negative control RC7: ' + case +
                              ' freshness failure (bindless=' +
                              str(bindless) + ')')
                    else:
                        if result.returncode:
                            raise RuntimeError(result.stdout + result.stderr)
                        print(result.stdout, end='')

    run(False)
    if args.negative_control:
        run(True)


if __name__ == '__main__':
    main()
