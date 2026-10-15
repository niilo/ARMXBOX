#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise pending texture changes through real descriptor preparation."""
import argparse
import os
from pathlib import Path
import re
import resource
import shlex
import subprocess
import tempfile

BASELINE = 'd2da241bab37c6460fc8832e8ded75d42362e739'


def block(text, start):
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def function(text, name):
    match = re.search(r'(?m)^(?:static\s+)?(?:inline\s+)?\w+\s+\**' +
                      name + r'\s*\([^;{}]*\)\s*\{', text)
    if not match:
        raise ValueError('missing function: ' + name)
    return block(text, match.start())


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
    prefix = 'hw/xbox/nv2a/pgraph/'

    for original in (False, True) if args.negative_control else (False,):
        def read(path):
            if original:
                return subprocess.check_output(
                    ['git', 'show', args.baseline_ref + ':' + prefix + path],
                    cwd=repo, text=True)
            return (repo / prefix / path).read_text()

        texture, shaders = read('vk/texture.c'), read('vk/shaders.c')
        draw, common = read('vk/draw.c'), read('pgraph.c')
        header = read('pgraph.h')
        method = block(common, common.index(
            'DEF_METHOD(NV097, SET_TEXTURE_OFFSET)'))
        method = method[method.index('{'):]
        create = function(texture, 'create_texture')
        start = create.index('if (binding_found) {\n        NV2A_VK_DPRINTF')
        cache_selection = block(create, start)
        poll_start = draw.index('if (pgraph_vk_poll_texture_memory_dirty(d))')
        pipeline = function(draw, 'create_pipeline')
        bind_start = pipeline.index('if (pg->texture_state_gen !=')
        pieces = [function(header, name) for name in
                  ('pgraph_reg_r', 'pgraph_reg_w', 'pgraph_is_texture_enabled')]
        pieces += ['static void set_offset(PGRAPHState *pg, unsigned method, '
                   'uint32_t parameter)\n' + method]
        pieces += [function(texture, name) for name in
                   ('mark_texture_range_dirty', 'check_texture_dirty',
                    'pgraph_vk_poll_texture_memory_dirty',
                    'resolve_possibly_dirty_textures', 'check_textures_dirty',
                    'update_timestamps')]
        if not original:
            pieces += [function(texture, 'texture_uniform_scale')]
        pieces += [
            'static void create_texture(PGRAPHState *pg, int texture_idx)\n'
            '{ PGRAPHVkState *r = pg->vk_renderer_state;\n'
            'TextureBinding *snode = lookup_cached_texture('
            'pgraph_reg_r(pg, NV_PGRAPH_TEXOFFSET0 + texture_idx * 4));\n'
            'bool binding_found = true, possibly_dirty = false;\n'
            'bool surface_to_texture = false, possibly_dirty_checked = false;\n'
            + cache_selection + '\n(void)possibly_dirty;\n'
            '(void)possibly_dirty_checked;\n}\n']
        pieces += [function(texture, 'pgraph_vk_bind_textures')]
        pieces += [function(draw, 'texture_bind_preserves_draw_state')]
        uniform_update = function(shaders, 'pgraph_vk_update_shader_uniforms')
        start = uniform_update.index('for (int i = 0; i < 4; i++)')
        pieces += ['static void get_texture_scales(PGRAPHState *pg, '
                   'PshUniformValues *values) {\n'
                   'PGRAPHVkState *r = pg->vk_renderer_state;\n'
                   'PshUniformValues psh_values;\n' +
                   block(uniform_update, start) + '\n*values = psh_values;\n}']
        pieces += ['static void poll_before_draw(NV2AState *d) {\n' +
                   block(draw, poll_start) + '\n}']
        pieces += ['static void pipeline_bind(PGRAPHState *pg) {\n'
                   'PGRAPHVkState *r = pg->vk_renderer_state;\n'
                   'NV2AState *d = container_of(pg, NV2AState, pgraph);\n' +
                   block(pipeline, bind_start) + '\n}']
        pieces += [function(shaders, name) for name in
                   ('use_push_descriptors', 'use_desc_set_cache',
                    'make_desc_set_cache_key', 'reuse_cached_descriptor_set',
                    'pgraph_vk_update_descriptor_sets')]
        pieces += [function(draw, name) for name in
                   ('push_texture_descriptors', 'bind_descriptor_sets',
                    'bind_bindless_set', 'push_texture_indices')]
        begin = function(draw, 'begin_draw')
        start = begin.index('if (!pg->clearing) {')
        pieces += ['static void consume_draw(PGRAPHState *pg) {\n'
                   'PGRAPHVkState *r = pg->vk_renderer_state;\n' +
                   block(begin, start) + '\n}']
        start = draw.index('r->need_descriptor_rebind = true;')
        end = draw.index('r->queries_reset_in_cb = false;', start)
        end += len('r->queries_reset_in_cb = false;')
        pieces += ['static void reset_command_state(PGRAPHVkState *r) {\n' +
                   draw[start:end] + '\n}']
        with tempfile.TemporaryDirectory(prefix='texture-notification-') as tmp:
            out = Path(tmp)
            (out / 'texture-notification-functions.inc').write_text(
                '\n\n'.join(pieces))
            for bindless in (0, 1):
                binary = out / ('fixture-' + str(bindless))
                command = shlex.split(args.cc) + [
                    '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-sign-compare', '-Wno-unused-function',
                    '-Wno-unused-variable', '-UNDEBUG',
                    '-Wno-unused-but-set-variable',
                    '-ftrivial-auto-var-init=pattern',
                    '-DOPT_BINDLESS_TEXTURES=' + str(bindless),
                    '-I' + tmp, '-I' + str(repo),
                    str(here / 'texture-notification-test.c'),
                    '-o', str(binary)]
                if args.sanitize:
                    command += ['-fsanitize=address,undefined',
                                '-fno-omit-frame-pointer']
                subprocess.run(command, check=True)
                result = subprocess.run([str(binary)], capture_output=True,
                                        text=True,
                                        preexec_fn=disable_core_dumps)
                if original:
                    expected = 'texture notification lost before descriptor'
                    if result.returncode == 0 or expected not in result.stderr:
                        raise RuntimeError('unexpected negative control: ' +
                                           result.stdout + result.stderr)
                    print('negative control: RC7 loses pending texture '
                          'descriptor change (bindless compiled=' +
                          str(bindless) + ')')
                elif result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                else:
                    print(result.stdout, end='')
                result = subprocess.run(
                    [str(binary), 'scale'], capture_output=True, text=True,
                    preexec_fn=disable_core_dumps)
                if original:
                    expected = 'texture scale change escaped fast-path guard'
                    if result.returncode == 0 or expected not in result.stderr:
                        raise RuntimeError('unexpected scale control: ' +
                                           result.stdout + result.stderr)
                    print('negative control: RC7 skips changed texScale '
                          '(bindless compiled=' + str(bindless) + ')')
                elif result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                else:
                    print(result.stdout, end='')
                environment = dict(os.environ, XEMU_VK_DESC_CACHE='0')
                result = subprocess.run(
                    [str(binary), 'cache-off'], capture_output=True,
                    text=True, env=environment,
                    preexec_fn=disable_core_dumps)
                if original:
                    expected = 'plain cache OFF descriptor image invalid'
                    if result.returncode == 0 or expected not in result.stderr:
                        raise RuntimeError('unexpected cache-OFF control: ' +
                                           result.stdout + result.stderr)
                    print('negative control: RC7 cache OFF uses invalid '
                          'descriptor images (bindless compiled=' +
                          str(bindless) + ')')
                elif result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                else:
                    print(result.stdout, end='')

                if original:
                    # Initialize only the old descriptor key to isolate its
                    # separate, still-invalid cache[-1] store. The immutable
                    # source above already proved the real image failure.
                    source = '\n\n'.join(pieces)
                    source = source.replace(
                        'struct DescSetCacheKey desc_key;',
                        'struct DescSetCacheKey desc_key;\n'
                        'make_desc_set_cache_key(r, layouts, &desc_key);')
                    (out / 'texture-notification-functions.inc').write_text(
                        source)
                    subprocess.run(command, check=True)
                    isolated = subprocess.run(
                        [str(binary), 'cache-off'], capture_output=True,
                        text=True, env=environment,
                        preexec_fn=disable_core_dumps)
                    expected = 'plain cache OFF descriptor sentinel corrupted'
                    if (isolated.returncode == 0 or
                            expected not in isolated.stderr):
                        raise RuntimeError('unexpected cache-slot control: ' +
                                           isolated.stdout + isolated.stderr)
                    print('isolated RC7 cache-slot control: corrected key '
                          'still corrupts the sentinel before cache[0] '
                          '(bindless compiled=' + str(bindless) + ')')
                    (out / 'texture-notification-functions.inc').write_text(
                        '\n\n'.join(pieces))

if __name__ == '__main__':
    main()
