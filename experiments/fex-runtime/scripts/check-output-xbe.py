#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent generated output descriptor, payload and GNU IA32 code oracle."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
parser=argparse.ArgumentParser()
parser.add_argument('image',type=Path)
args=parser.parse_args()
image=args.image.read_bytes()
word=lambda offset: struct.unpack_from('<I',image,offset)[0]
assert len(image)==12288 and image[:4]==b'XBEH'
for offset,value in {0x104:0x10000,0x108:4096,0x10c:16384,0x110:376,
                     0x118:0x10300,0x11c:2,0x120:0x10180,0x300:464,
                     0x2000:0x8000011c,0x2004:0}.items(): assert word(offset)==value
assert word(0x128)^0xa8fc57ab==0x11200
assert word(0x158)^0x5b6d40b6==0x12000
for offset,fields in [(0x180,(6,0x11000,4096,4096,4096)),(0x1b8,(3,0x12000,8192,8192,4096))]:
    assert struct.unpack_from('<5I',image,offset)==fields
base=0x13000
wire=bytearray(384)
fields=[0x3154554f,1,1,0x13080,24,0x130a0,64,4,4,0x13100,64,0x54494d43,1,1,1,48000]
struct.pack_into('<16I',wire,0,*fields)
struct.pack_into('<6f',wire,0x80,-1,-1,-1,1,1,-1)
for y in range(4):
    for x in range(4): wire[0xa0+4*(4*y+x):0xa4+4*(4*y+x)]=bytes((240,32,80,255) if (x^y)&1 else (16,208,96,255))
for i in range(64): struct.pack_into('<h',wire,0x100+2*i,((i%16)-8)*512)
lines=['.code32','.text','movl $0, 0x1302c','movl %eax, 0x13008']
for begin,end in [(0,64),(0x80,0x98),(0xa0,0xe0),(0x100,0x180)]:
    for at in range(begin,end,4):
        if at not in (8,44):lines.append(f'movl ${struct.unpack_from("<I",wire,at)[0]:#x}, {base+at:#x}')
lines+=['movl $0x54494d43, 0x1302c','hlt']
assert len(lines)-2==72
with tempfile.TemporaryDirectory(prefix='armxbox-output-asm-') as directory:
    root=Path(directory);(root/'output.s').write_text('\n'.join(lines)+'\n')
    subprocess.run(['as','--32',str(root/'output.s'),'-o',str(root/'output.o')],check=True)
    subprocess.run(['objcopy','-O','binary','-j','.text',str(root/'output.o'),str(root/'output.bin')],check=True)
    code=(root/'output.bin').read_bytes()
    assert len(code)==706 and image[0x1200:0x1200+len(code)]==code
print('PASS independent output XBE descriptor/triangle/checkerboard/PCM and all 72 GNU IA32 instructions')
print('Output XBE SHA256:',hashlib.sha256(image).hexdigest())
print('Output payload SHA256:',hashlib.sha256(wire).hexdigest())
