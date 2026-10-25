#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent binary/header and GNU assembler check for our generated XBE."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('image', type=Path)
args = parser.parse_args()
data = args.image.read_bytes()
assert len(data) == 12288 and data[:4] == b'XBEH'
word = lambda offset: struct.unpack_from('<I', data, offset)[0]
assert word(0x104) == 0x00010000 and word(0x108) == 4096
assert word(0x10c) == 16384 and word(0x110) == 376
assert word(0x118) == 0x00010300 and word(0x300) == 464
assert word(0x11c) == 2 and word(0x120) == 0x00010180
assert word(0x128) ^ 0xa8fc57ab == 0x00011000
assert word(0x158) ^ 0x5b6d40b6 == 0x00012000
assert word(0x2000) == 0x8000011c and word(0x2004) == 0
for offset, expected in [(0x180, (6, 0x00011000, 4096, 4096, 4096)),
                         (0x1b8, (3, 0x00012000, 8192, 8192, 4096))]:
    assert struct.unpack_from('<5I', data, offset) == expected
with tempfile.TemporaryDirectory(prefix='armxbox-xbe-asm-') as directory:
    root = Path(directory)
    source = root / 'fixture.s'
    source.write_text('''.code32
.text
pushl $0x5a
pushl $32
pushl $0x00013000
call *0x00012000
movl 0x00013000, %eax
movl %eax, 0x00012020
hlt
''')
    subprocess.run(['as', '--32', str(source), '-o', str(root / 'fixture.o')], check=True)
    subprocess.run(['objcopy', '-O', 'binary', '-j', '.text', str(root / 'fixture.o'),
                    str(root / 'fixture.bin')], check=True)
    code = (root / 'fixture.bin').read_bytes()
    assert len(code) == 26 and data[4096:4096+len(code)] == code
assert data[0x1100] == 0xf4
print('PASS independent XBE header/ordinal284 and GNU IA32 instruction encoding')
print('Generated XBE SHA256:', hashlib.sha256(data).hexdigest())
