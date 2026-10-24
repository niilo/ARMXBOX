#!/usr/bin/env python3
"""Fetch immutable dependencies and apply the reviewed Android/IA-32 patches."""
import argparse
import json
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
    patches = [ROOT / 'patches/0001-bionic-getdents.diff', ROOT / 'patches/0002-ia32-divide-errors.diff']
    for patch in patches:
        reverse = subprocess.run(['git', '-C', str(checkout), 'apply', '--reverse', '--check', str(patch)],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if reverse.returncode:
            run('git', '-C', str(checkout), 'apply', '--check', str(patch))
            run('git', '-C', str(checkout), 'apply', str(patch))
    # Git orders a whole-tree diff by path; independent patches may be listed in
    # a different order. Compare exact per-file diff blocks, including extra edits.
    def normalized(diff):
        blocks = diff.split('diff --git ')
        if blocks[0].strip():
            raise SystemExit('Unexpected patch preamble')
        return sorted('diff --git ' + block for block in blocks[1:])
    expected = ''.join(p.read_text() for p in patches)
    actual = run('git', '-C', str(checkout), 'diff', capture_output=True).stdout
    if normalized(actual) != normalized(expected):
        raise SystemExit('Dependency diff differs from reviewed portability/CPU patches')
    print(f'FEX {LOCK["commit"]}; dependency pins and portability diff verified')

if __name__ == '__main__':
    main()
