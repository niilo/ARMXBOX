#!/usr/bin/env python3
"""Capture selected IA32 instruction admission on the Xbox reference emulator.

Generated protected-mode ROM, not proprietary firmware. Requires GNU as/ld,
a built ARMXBOX desktop binary and a display (xvfb-run in headless Linux).
Captures compatibility behavior; does not certify physical Pentium III behavior.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time

CASES = (
    ('sse1_xorps', '0f57c0'),
    ('mmx_pxor', '0fefc0'),
    ('x87_fld1', 'd9e8'),
    ('sse2_pxor', '660fefc0'),
    ('sse3_haddps', 'f20f7cc0'),
    ('ssse3_pshufb', '660f3800c0'),
    ('sse41_ptest', '660f3817c0'),
    ('sse42_pcmpgtq', '660f3837c0'),
    ('avx_vzeroupper', 'c5f877'),
    ('rdtscp', '0f01f9'),
    ('xgetbv', '0f01d0'),
    ('ud2', '0f0b'),
)

# LGDT uses an explicit 32-bit operand: the reset CS hidden base is high,
# and a 16-bit LGDT would truncate the GDT base to 24 bits and triple fault.
BOOT = """.intel_syntax noprefix
.section .text
.global _start
.code16
_start:
cli
data32 lgdt cs:[gdtr_offset]
mov eax,cr0
or eax,1
mov cr0,eax
.byte 0x66,0xea
.long protected
.word 8
.code32
protected:
mov ax,16
mov ds,ax
mov es,ax
mov ss,ax
mov esp,0x8000
lidt [idtr]
mov eax,cr0
and eax,0xfffffff3
or eax,0x23
mov cr0,eax
mov eax,0x600
mov cr4,eax"""

# #UD has no hardware error word; add a dummy before the trap number.
# After PUSHAD, trap/error/EIP/CS/EFLAGS are at ESP+32/36/40/44/48.
TAIL = """mov word ptr [0x1ff0],0xcafe
hlt
jmp .-1
isr6:
push 0
push 6
jmp trap
isr13:
push 13
jmp trap
isr14:
push 14
jmp trap
trap:
pushad
mov eax,[0x1fd0]
mov edx,[esp+32]
mov [eax],edx
mov edx,[esp+36]
mov [eax+4],edx
mov edx,[esp+40]
mov [eax+8],edx
mov edx,[0x1fd4]
mov [esp+40],edx
popad
add esp,8
iretd
.balign 8
gdt:
.quad 0,0x00cf9a000000ffff,0x00cf92000000ffff
gdt_end:
gdtr:
.set gdtr_offset,gdtr-_start
.word gdt_end-gdt-1
.long gdt
.balign 8
idt:
.rept 6
.quad 0
.endr
.word isr6-_start,8
.byte 0,0x8e
.word 0xffff
.rept 6
.quad 0
.endr
.word isr13-_start,8
.byte 0,0x8e
.word 0xffff
.word isr14-_start,8
.byte 0,0x8e
.word 0xffff
.quad 0
idt_end:
idtr:
.word idt_end-idt-1
.long idt
.org 0xfff0
.code16
.byte 0xe9
.word _start-.-2
.fill 13,1,0xff"""


def assembly():
    asm = BOOT.splitlines()
    for i, (_, opcode) in enumerate(CASES*2):
        base = 0x2000+i*32
        for offset in range(0, 32, 4):
            asm.append(f'mov dword ptr [{base+offset}],0')
        asm.extend([
            f'mov dword ptr [{base+28}], OFFSET FLAT:op_{i}',
            f'mov dword ptr [0x1fd0],{base}',
            f'mov dword ptr [0x1fd4],OFFSET FLAT:next_{i}',
            'fninit', 'push 0x8d7', 'popfd',
            'mov eax,0x12345678', 'mov ebx,0x22334455',
            'xor ecx,ecx', 'mov edx,0x55667788',
            'push 0x8d7', 'popfd', f'op_{i}:',
            '.byte '+','.join('0x'+opcode[j:j+2] for j in range(0, len(opcode), 2)),
            f'mov dword ptr [{base+16}],0xfeedface', f'next_{i}:',
            f'mov dword ptr [{base+20}],eax', f'mov dword ptr [{base+24}],edx',
            'pushfd', f'pop dword ptr [{base+12}]'])
    asm.extend(TAIL.splitlines())
    return '\n'.join(asm)+'\n'


def validate(words):
    if len(words) != len(CASES)*16:
        raise RuntimeError("incomplete instruction result table")
    normalized = []
    for i, (name, opcode) in enumerate(CASES*2):
        row = words[i*8:i*8+8]
        trap, error, pc, flags, sentinel, eax, edx, expected_pc = row
        if flags != 0x8d7 or eax != 0x12345678 or edx != 0x55667788:
            raise RuntimeError(f"{name}: fixture state was not preserved")
        if error != 0 or trap not in (0, 6):
            raise RuntimeError(f"{name}: unexpected trap/error")
        if (trap == 0 and (pc != 0 or sentinel != 0xfeedface)) or (
                trap == 6 and (pc != expected_pc or sentinel != 0)):
            raise RuntimeError(f"{name}: invalid PC/sentinel result")
        normalized.append([trap, error, bool(pc == expected_pc), flags, sentinel, eax, edx])
    half = len(CASES)
    if normalized[:half] != normalized[half:]:
        raise RuntimeError("repeated instruction outcomes differ")
    if any(normalized[i][0] for i in range(3)) or normalized[half-1][0] != 6:
        raise RuntimeError("SSE1/MMX/x87/UD2 controls did not pass")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--reference-commit', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch('[0-9a-f]{40}', args.reference_commit):
        parser.error('reference commit must be an immutable full SHA')
    root, binary = args.output.resolve(), args.binary.resolve()
    root.mkdir(parents=True, exist_ok=True)
    if (root/'capture.toml').exists():
        parser.error('use a fresh output directory')
    (root/'data').mkdir(exist_ok=True)
    source = assembly()
    (root/'fixture.S').write_text(source)
    subprocess.run(['as', '--32', str(root/'fixture.S'), '-o', str(root/'fixture.o')], check=True)
    subprocess.run(['ld', '-m', 'elf_i386', '--oformat', 'binary', '-Ttext=0xffff0000',
                    '-e', '_start', str(root/'fixture.o'), '-o', str(root/'rom64.bin')], check=True)
    code = (root/'rom64.bin').read_bytes()
    if len(code) != 65536:
        raise RuntimeError('incorrect ROM extent')
    rom = bytes([255])*0x70000 + code
    (root/'fixture.bin').write_bytes(rom)
    (root/'capture.toml').write_text('[general]\nshow_welcome = false\n[sys.files]\n'
        f'flashrom_path = {json.dumps(str(root/"fixture.bin"))}\n'
        f'eeprom_path = {json.dumps(str(root/"eeprom.bin"))}\n')
    with tempfile.TemporaryDirectory(prefix='armxbox-isa-') as temporary:
        endpoint = Path(temporary)/'qmp.sock'
        with (root/'emulator.log').open('w') as log:
            proc = subprocess.Popen([str(binary), '-config_path', str(root/'capture.toml'),
                '-accel', 'tcg', '-smp', '1', '-qmp',
                f'unix:{endpoint},server=on,wait=off', '-S', '-nic', 'none'],
                stdout=log, stderr=log, env=dict(os.environ, LIBGL_ALWAYS_SOFTWARE='1',
                    SDL_AUDIODRIVER='dummy', QEMU_AUDIO_DRV='sdl', XDG_DATA_HOME=str(root/'data')))
            try:
                with socket.socket(socket.AF_UNIX) as conn:
                    conn.settimeout(10)
                    deadline = time.monotonic()+30
                    while True:
                        try:
                            conn.connect(str(endpoint))
                            break
                        except (FileNotFoundError, ConnectionRefusedError):
                            if proc.poll() is not None or time.monotonic() > deadline:
                                raise RuntimeError('reference startup failed; see emulator.log')
                            time.sleep(.1)
                    with conn.makefile('rwb', buffering=0) as stream:
                        if 'QMP' not in json.loads(stream.readline()):
                            raise RuntimeError('invalid QMP greeting')

                        def qmp(command, arguments=None):
                            request = {'execute': command}
                            if arguments is not None:
                                request['arguments'] = arguments
                            stream.write(json.dumps(request).encode()+b'\n')
                            while True:
                                response = json.loads(stream.readline())
                                if 'return' in response:
                                    return response['return']
                                if 'error' in response:
                                    raise RuntimeError(response['error'])

                        def monitor(command):
                            return qmp('human-monitor-command', {'command-line': command})

                        qmp('qmp_capabilities')
                        qmp('cont')
                        deadline = time.monotonic()+10
                        while '0xcafe' not in monitor('xp /1hx 0x1ff0'):
                            if time.monotonic() > deadline:
                                qmp('stop')
                                (root/'failure-registers.txt').write_text(monitor('info registers'))
                                raise RuntimeError('ROM did not complete')
                            time.sleep(.05)
                        qmp('stop')
                        registers = monitor('info registers')
                        if not all(x in registers for x in ('HLT=1', 'CPL=0', 'CS32', 'CR4=00000600')):
                            raise RuntimeError('unexpected reference execution mode')
                        dump = monitor(f'xp /{len(CASES)*16}wx 0x2000')
                        words = [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{8})\b', dump)]
                        validate(words)
                        qmp('quit')
                proc.wait(timeout=10)
                if proc.returncode != 0:
                    raise RuntimeError('reference process exit failed')
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait()
    actual = re.search(r'^xemu_commit: ([0-9a-f]{40})$', (root/'emulator.log').read_text(), re.M)
    if not actual or actual[1] != args.reference_commit:
        raise RuntimeError('reference binary revision mismatch')
    result = {'schema': 1, 'reference_commit': actual[1],
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'firmware_sha256': hashlib.sha256(rom).hexdigest(),
        'assembly_sha256': hashlib.sha256(source.encode()).hexdigest(),
        'machine': 'xbox', 'cpu': 'pentium3', 'memory_mib': 64,
        'execution': 'TCG protected IA32 CPL0; CR4=0x600; generated ROM; two passes; normal exit',
        'provenance': 'reference-emulator compatibility capture, not physical Xbox ISA certification',
        'word_order': ['trap', 'error', 'fault_pc', 'eflags', 'following_sentinel', 'eax', 'edx', 'opcode_pc'],
        'cases': []}
    for i, (name, opcode) in enumerate(CASES):
        passes = []
        for run in range(2):
            start = (i+run*len(CASES))*8
            passes.append([f'{x:08x}' for x in words[start:start+8]])
        result['cases'].append({'name': name, 'opcode': opcode, 'passes': passes})
    (root/'instructions.json').write_text(json.dumps(result, indent=2)+'\n')
    (root/'registers.txt').write_text(registers)
    (root/'memory.txt').write_text(dump)
    print(f'PASS {len(CASES)} reference instruction cases repeated twice; normal exit')


if __name__ == '__main__':
    main()
