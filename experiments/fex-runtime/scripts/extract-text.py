#!/usr/bin/env python3
"""Extract relocation-free .text from our trusted AArch64 ELF64 test object."""
from pathlib import Path
import struct
import sys

blob = Path(sys.argv[1]).read_bytes()
header = struct.unpack_from('<16sHHIQQQIHHHHHH', blob)
if blob[:6] != b'\x7fELF\x02\x01' or header[2] != 183:
    raise SystemExit('Expected little-endian AArch64 ELF64 object')
shoff, shsize, count, names_index = header[6], header[11], header[12], header[13]
sections = [struct.unpack_from('<IIQQQQIIQQ', blob, shoff + i * shsize) for i in range(count)]
names_section = sections[names_index]
names = blob[names_section[4]:names_section[4] + names_section[5]]
text = None
for section in sections:
    name = names[section[0]:].split(b'\0', 1)[0]
    if name in (b'.rela.text', b'.rel.text') and section[5]:
        raise SystemExit('Witness must have no text relocations')
    if name == b'.text':
        text = blob[section[4]:section[4] + section[5]]
if not text or len(text) % 4:
    raise SystemExit('Missing or malformed AArch64 instruction stream')
Path(sys.argv[2]).write_bytes(text)
