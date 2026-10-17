#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check real surface-cache branches and descriptor input construction."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def block(source, start):
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end], end


def function(source, name):
    match = re.search(r'(?m)^(?:static\s+)?(?:inline\s+)?\w+\s+\**' +
                      name + r'\s*\([^;{}]*\)\s*\{', source)
    if not match:
        raise ValueError('missing function: ' + name)
    start = source.rfind('\n', 0, match.start()) + 1
    return block(source, start)[0]


def cached_branch(source):
    create = function(source, 'create_texture')
    marker = 'bool surface_to_texture = false;'
    start = create.index(marker) + len(marker)
    end_marker = 'r->tex_surface_direct[texture_idx] = false;'
    end = create.index(end_marker, start) + len(end_marker)
    prelude = create[start:end]
    start = create.index('if (binding_found) {\n        bool did_s2t_copy')
    start = create.index('if (surface_to_texture)', start)
    first, end = block(create, start)
    second, _ = block(create, create.index('else', end))
    return '''static void exercise_cached(PGRAPHState *pg, int texture_idx,
                                      SurfaceBinding *surface,
                                      TextureBinding *snode,
                                      bool surface_to_texture)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    bool did_s2t_copy = false, did_upload = false;
    bool was_surface_copy = snode->draw_time != 0;
    bool possibly_dirty = true;
    uint64_t content_hash = snode->hash + 1;
''' + prelude + '\n' + first + ' ' + second + '''
    (void)did_s2t_copy;
    (void)did_upload;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--baseline-ref',
                        default='f75153dddd03880c0c6fbfea93b2dea8ceb29af9')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]

    def run(original, bindless):
        def read(name):
            path = 'hw/xbox/nv2a/pgraph/vk/' + name
            if original:
                return subprocess.check_output(
                    ['git', 'show', args.baseline_ref + ':' + path],
                    cwd=repo, text=True)
            return (repo / path).read_text()
        texture, shaders = read('texture.c'), read('shaders.c')
        pieces = [function(texture, 'bind_surface_as_texture'),
                  function(texture, 'bind_zeta_surface_as_texture')]
        if not original:
            invalidator = 'pgraph_vk_invalidate_surface_bindless_view'
            pieces.append(function(texture, invalidator))
            if bindless:
                pieces.append(function(texture,
                                       'update_bindless_texture_descriptor'))
            pieces.append(function(texture, 'bind_surface_texture_direct'))
        if not original:
            pieces.append(function(read('draw.c'),
                                   'texture_bind_preserves_draw_state'))
        else:
            # The original negative control aborts at stale descriptor inputs
            # before this later guard test is reached.
            pieces.append('static bool texture_bind_preserves_draw_state('
                          'const PGRAPHVkState *r) { (void)r; return true; }')
            pieces.append('static void '
                          'pgraph_vk_invalidate_surface_bindless_view('
                          'PGRAPHVkState *r, VkImageView v) '
                          '{ (void)r; (void)v; }')
        pieces += [function(texture, 'update_timestamps'),
                   function(shaders, 'make_desc_set_cache_key'),
                   cached_branch(texture)]
        with tempfile.TemporaryDirectory(prefix='surface-reuse-') as directory:
            output = Path(directory)
            (output / 'surface-reuse-functions.inc').write_text(
                '\n\n'.join(pieces) + '\n')
            binary = output / 'surface-reuse'
            command = shlex.split(args.cc) + [
                '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-UNDEBUG',
                '-Wno-unused-function', '-Wno-sign-compare',
                '-DOPT_BINDLESS_TEXTURES=' + str(bindless), '-I' + directory,
                str(Path(__file__).with_suffix('.c')), '-o', str(binary)]
            if args.sanitize:
                command += ['-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer']
            subprocess.run(command, cwd=repo, check=True)
            result = subprocess.run([str(binary)], capture_output=True,
                                    text=True)
            if original:
                expected = 'cached surface descriptor inputs are stale'
                if result.returncode == 0 or expected not in result.stderr:
                    raise RuntimeError('unexpected negative control: ' +
                                       result.stdout + result.stderr)
                print('negative control: original cached branch leaves '
                      f'stale descriptor inputs (bindless={bindless})')
            else:
                if result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                print(result.stdout, end='')

    for bindless in (0, 1):
        run(False, bindless)
        if args.negative_control:
            run(True, bindless)


if __name__ == '__main__':
    main()
