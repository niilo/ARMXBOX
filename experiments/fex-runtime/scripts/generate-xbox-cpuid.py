#!/usr/bin/env python3
"""Generate the C++ fixture table from captured reference data; --check for CI."""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    data = json.loads((root / 'reference/xbox-cpuid.json').read_text())
    assert data['schema'] == 1
    assert data['register_order'] == ['eax', 'ebx', 'ecx', 'edx']
    assert len(data['cases']) == 54
    text = '''// Generated from reference/xbox-cpuid.json; see scripts/capture-xbox-cpuid.py.
#pragma once
#include <array>
#include <cstdint>
namespace armxbox {
struct CPUIDCase {
    uint32_t eax, ecx;
    std::array<uint32_t, 4> result;
};
inline constexpr CPUIDCase XboxCPUIDCases[] = {
'''
    for case in data['cases']:
        values = [case['eax'], case['ecx'], *case['result']]
        assert len(values) == 6
        assert all(len(v) == 8 and f'{int(v, 16):08x}' == v for v in values)
        text += ('    {0x' + case['eax'] + ', 0x' + case['ecx'] + ', {' +
                 ', '.join('0x' + v for v in case['result']) + '}},\n')
    text += '};\n} // namespace armxbox\n'
    output = root / 'reference/xbox_cpuid_cases.h'
    if args.check:
        if output.read_text() != text:
            raise SystemExit('Reference header differs from captured JSON; regenerate it')
        print('PASS generated CPUID table matches all 54 captured reference cases')
    else:
        output.write_text(text)


if __name__ == '__main__':
    main()
