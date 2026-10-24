#!/usr/bin/env python3
"""Execute a generated real-mode CPUID ROM on the realized Xbox reference CPU.

Requires a built ARMXBOX desktop binary; run under xvfb-run in headless Linux.
No proprietary firmware or game data is used. This never queries host CPUID.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import time

LEAVES = (0, 1, 2, 3, 4, 7, 0xD, 0x16, 0x40000000, 0x40000001,
          0x40000002, 0x80000000, 0x80000001, 0x80000002, 0x80000003,
          0x80000004, 0xC0000000, 0xFFFFFFFF)
INPUTS = [(a, c) for a in LEAVES for c in (0, 1, 0xFFFFFFFF)]
TABLE = 0x2000
MARKER = 0x1FF0


def firmware(inputs):
    # Reset CS has hidden base 0xffff0000. Keep it using a near 16-bit jump.
    # CLI; XOR AX,AX; MOV DS,AX. Operand-size overrides expose 32-bit CPUID data.
    code = bytearray.fromhex('fa31c08ed8')
    for i, (eax, ecx) in enumerate(inputs):
        code += b'\x66\xb8' + struct.pack('<I', eax)
        code += b'\x66\xb9' + struct.pack('<I', ecx) + b'\x0f\xa2'
        for reg, modrm in enumerate((None, 0x1E, 0x0E, 0x16)):
            code += b'\x66\xa3' if reg == 0 else b'\x66\x89' + bytes([modrm])
            code += struct.pack('<H', TABLE + 16 * i + 4 * reg)
    # Publish completion only after all results, then remain halted with IRQs off.
    code += bytes.fromhex('c706f01ffecaf4ebfd')
    assert len(code) < 0xFFF0 and TABLE + 16 * len(inputs) < 0x10000
    rom = bytearray(b'\xff' * 0x80000)
    rom[-65536:-65536 + len(code)] = code
    rom[-16:-13] = bytes.fromhex('e90d00')
    return bytes(rom)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--reference-commit', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch('[0-9a-f]{40}', args.reference_commit):
        parser.error('--reference-commit must be a full immutable Git commit')
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / 'data').mkdir(exist_ok=True)
    # A dedicated directory avoids changing the user's emulator configuration.
    config = output / 'capture.toml'
    if config.exists():
        parser.error('use a fresh output directory for each reference capture')
    rom = firmware(INPUTS * 2)
    (output / 'fixture.bin').write_bytes(rom)
    config.write_text('[general]\nshow_welcome = false\n[sys.files]\n'
                      f'flashrom_path = {json.dumps(str(output / "fixture.bin"))}\n'
                      f'eeprom_path = {json.dumps(str(output / "eeprom.bin"))}\n')
    # Keep the AF_UNIX path short even when output is under a long CI prefix.
    import tempfile
    with tempfile.TemporaryDirectory(prefix='armxbox-cpuid-') as temporary:
        endpoint = Path(temporary) / 'qmp.sock'
        with (output / 'emulator.log').open('w') as log:
            proc = subprocess.Popen(
                [str(binary), '-config_path', str(config), '-accel', 'tcg', '-smp', '1', '-qmp',
                 f'unix:{endpoint},server=on,wait=off', '-S', '-nic', 'none'],
                stdout=log, stderr=log,
                env=dict(os.environ, LIBGL_ALWAYS_SOFTWARE='1',
                         SDL_AUDIODRIVER='dummy', QEMU_AUDIO_DRV='sdl',
                         XDG_DATA_HOME=str(output / 'data')))
            try:
                with socket.socket(socket.AF_UNIX) as connection:
                    connection.settimeout(10)
                    deadline = time.monotonic() + 30
                    while True:
                        if proc.poll() is not None:
                            raise RuntimeError('reference exited; see emulator.log')
                        try:
                            connection.connect(str(endpoint))
                            break
                        except (FileNotFoundError, ConnectionRefusedError):
                            if time.monotonic() >= deadline:
                                raise TimeoutError('reference QMP startup timeout')
                            time.sleep(0.1)
                    with connection.makefile('rwb', buffering=0) as stream:
                        greeting = json.loads(stream.readline())
                        assert 'QMP' in greeting

                        def qmp(command, arguments=None):
                            request = {'execute': command}
                            if arguments is not None:
                                request['arguments'] = arguments
                            stream.write(json.dumps(request).encode() + b'\n')
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
                        deadline = time.monotonic() + 10
                        while '0xcafe' not in monitor(f'xp /1hx {MARKER:#x}'):
                            if time.monotonic() >= deadline:
                                raise TimeoutError('CPUID ROM did not complete')
                            time.sleep(0.05)
                        qmp('stop')
                        registers = monitor('info registers')
                        if 'HLT=1' not in registers or 'CPL=0' not in registers:
                            raise RuntimeError('reference did not reach expected real-mode halt')
                        dump = monitor(f'xp /{len(INPUTS) * 8}wx {TABLE:#x}')
                        words = [int(word, 16) for word in re.findall(r'0x([0-9a-fA-F]{8})\b', dump)]
                        assert len(words) == len(INPUTS) * 8
                        half = len(words) // 2
                        assert words[:half] == words[half:], 'repeated CPUID results differ'
                        qmp('quit')
                proc.wait(timeout=10)
                assert proc.returncode == 0, 'reference process exit failed'
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait()
    log = (output / 'emulator.log').read_text()
    actual = re.search(r'^xemu_commit: ([0-9a-f]{40})$', log, re.M)
    assert actual and actual[1] == args.reference_commit, 'binary revision mismatch'
    result = {
        'schema': 1, 'reference_commit': actual[1],
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'firmware_sha256': hashlib.sha256(rom).hexdigest(),
        'machine': 'xbox', 'cpu': 'pentium3', 'memory_mib': 64,
        'execution': 'TCG real mode; generated ROM; two identical passes; normal exit',
        'register_order': ['eax', 'ebx', 'ecx', 'edx'],
        'cases': [{'eax': f'{eax:08x}', 'ecx': f'{ecx:08x}',
                   'result': [f'{word:08x}' for word in words[4*i:4*i+4]]}
                  for i, (eax, ecx) in enumerate(INPUTS)]}
    (output / 'cpuid.json').write_text(json.dumps(result, indent=2) + '\n')
    (output / 'registers.txt').write_text(registers)
    (output / 'memory.txt').write_text(dump)
    print(f'PASS {len(INPUTS)} reference CPUID cases, repeated twice; normal process exit')


if __name__ == '__main__':
    main()
