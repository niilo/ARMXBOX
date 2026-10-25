#!/usr/bin/env python3
"""Generate/check the diagnostic instruction cases from executed reference data."""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    data = json.loads((root/'reference/xbox-instructions.json').read_text())
    assert data['schema'] == 1 and len(data['cases']) == 12
    assert [case['name'] for case in data['cases']] == [
        'sse1_xorps', 'mmx_pxor', 'x87_fld1', 'sse2_pxor', 'sse3_haddps',
        'ssse3_pshufb', 'sse41_ptest', 'sse42_pcmpgtq', 'avx_vzeroupper',
        'rdtscp', 'xgetbv', 'ud2']
    assert data['word_order'] == ['trap', 'error', 'fault_pc', 'eflags',
                                  'following_sentinel', 'eax', 'edx', 'opcode_pc']
    text = '''// Generated from reference/xbox-instructions.json; not FEX output.
#pragma once
#include <array>
#include <cstdint>
namespace armxbox {
struct InstructionCase {
    const char *name;
    std::array<uint8_t, 5> bytes;
    unsigned size;
    uint8_t trap;
};
inline constexpr InstructionCase XboxInstructionCases[] = {
'''
    for case in data['cases']:
        opcode = bytes.fromhex(case['opcode'])
        assert 0 < len(opcode) <= 5
        rows = [[int(x, 16) for x in row] for row in case['passes']]
        assert len(rows) == 2 and all(len(row) == 8 for row in rows)
        for row in rows:
            trap, error, pc, flags, sentinel, eax, edx, expected_pc = row
            assert trap in (0, 6) and error == 0 and flags == 0x8d7
            assert eax == 0x12345678 and edx == 0x55667788
            assert (pc == expected_pc and sentinel == 0) if trap else (pc == 0 and sentinel == 0xfeedface)
        assert rows[0][0] == rows[1][0]
        text += '    {"'+case['name']+'", {'+', '.join(f'0x{x:02x}' for x in opcode)+'}, '+str(len(opcode))+', '+str(rows[0][0])+'},\n'
    text += '};\n} // namespace armxbox\n'
    output = root/'reference/xbox_instruction_cases.h'
    if args.check:
        if output.read_text() != text:
            raise SystemExit('Reference instruction header differs; regenerate it')
        print('PASS generated instruction table matches all 12 reference captures')
    else:
        output.write_text(text)


if __name__ == '__main__':
    main()
