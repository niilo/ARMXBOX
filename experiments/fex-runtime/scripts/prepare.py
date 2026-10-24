#!/usr/bin/env python3
"""Fetch immutable dependencies and apply the reviewed Android/IA-32 patches."""
import argparse
import json
import os
import tempfile
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / 'dependencies.json').read_text())

def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('checkout', type=Path)
    args = parser.parse_args()
    checkout = args.checkout.resolve()
    if not checkout.exists():
        run('git', 'clone', '--no-checkout', LOCK['repository'], str(checkout))
        run('git', '-C', str(checkout), 'checkout', '--detach', LOCK['commit'])
    actual = run('git', '-C', str(checkout), 'rev-parse', 'HEAD', capture_output=True).stdout.strip()
    if actual != LOCK['commit']:
        dirty = run('git', '-C', str(checkout), 'status', '--porcelain', capture_output=True).stdout
        if dirty:
            raise SystemExit('Refusing to switch a modified dependency checkout')
        run('git', '-C', str(checkout), 'checkout', '--detach', LOCK['commit'])
    for path, pin in LOCK['submodules'].items():
        run('git', '-C', str(checkout), 'submodule', 'update', '--init', '--', path)
        actual = run('git', '-C', str(checkout / path), 'rev-parse', 'HEAD', capture_output=True).stdout.strip()
        if actual != pin:
            raise SystemExit(f'{path}: expected {pin}, got {actual}')
        if run('git', '-C', str(checkout / path), 'status', '--porcelain', capture_output=True).stdout:
            raise SystemExit(f'{path}: modified dependency checkout')
    patches = [ROOT / 'patches/0001-bionic-getdents.diff', ROOT / 'patches/0002-ia32-divide-errors.diff',
               ROOT / 'patches/0003-precise-memory-fault-state.diff']
    # Compose overlapping patches in an isolated index. Recognize exact reviewed
    # prefixes so repeat preparation never relies on reversing an older patch
    # whose context has legitimately been changed by a later patch.
    prefixes = ['']
    with tempfile.TemporaryDirectory(prefix='armxbox-fex-index-') as temporary:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(temporary) / 'index'))
        run('git', '-C', str(checkout), 'read-tree', 'HEAD', env=env)
        for patch in patches:
            run('git', '-C', str(checkout), 'apply', '--cached',
                '--whitespace=error', str(patch), env=env)
            prefixes.append(run('git', '-C', str(checkout), 'diff', '--cached',
                                capture_output=True, env=env).stdout)
    actual = run('git', '-C', str(checkout), 'diff', capture_output=True).stdout
    if actual not in prefixes:
        raise SystemExit('Dependency diff differs from reviewed portability/CPU patches')
    for patch in patches[prefixes.index(actual):]:
        run('git', '-C', str(checkout), 'apply', '--check', str(patch))
        run('git', '-C', str(checkout), 'apply', str(patch))
    actual = run('git', '-C', str(checkout), 'diff', capture_output=True).stdout
    if actual != prefixes[-1]:
        raise SystemExit('Dependency diff differs from reviewed portability/CPU patches')
    run('git', '-C', str(checkout), 'diff', '--check')
    print(f'FEX {LOCK["commit"]}; dependency pins and portability diff verified')

if __name__ == '__main__':
    main()
