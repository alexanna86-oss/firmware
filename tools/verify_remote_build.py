"""Validate target partition binary and factory composition before publishing."""
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

def verify(folder):
    folder = Path(folder)
    table = (folder / 'partitions.bin').read_bytes()
    entries = {}
    occupied = []
    for pos in range(0, len(table), 32):
        if table[pos:pos+2] != b'\xaa\x50':
            break
        _, typ, sub, offset, size, label, _ = struct.unpack('<HBBII16sI', table[pos:pos+32])
        label = label.split(b'\0')[0].decode()
        assert offset >= 0x9000 and size > 0 and offset + size <= 0x1000000, label
        assert offset % 0x1000 == 0 and (typ != 0 or offset % 0x10000 == 0), label
        entries[label] = (typ, sub, offset, size)
        occupied.append((offset, offset + size))
    for a, b in zip(sorted(occupied), sorted(occupied)[1:]):
        assert a[1] <= b[0], 'Overlapping partitions'
    assert entries['nvs'] == (1, 2, 0x9000, 0x6000)
    assert entries['app0'] == (0, 0x10, 0x10000, 0x470000)
    assert entries['app1'] == (0, 0x11, 0x480000, 0x470000)
    assert entries['otadata'] == (1, 0, 0xfee000, 0x2000)
    app = (folder / 'firmware.bin').read_bytes()
    boot = (folder / 'bootloader.bin').read_bytes()
    factory = (folder / 'firmware.factory.bin').read_bytes()
    assert app[0] == boot[0] == 0xe9
    assert struct.unpack_from('<H', app, 12)[0] == 9, 'Not an ESP32-S3 application'
    assert 0 < len(app) <= entries['app0'][3]
    assert factory[:len(boot)] == boot, 'Bootloader mismatch'
    assert factory[0x8000:0x8000+len(table)] == table, 'Partition table mismatch'
    assert factory[0x10000:0x10000+len(app)] == app, 'App mismatch'
    ota = factory[0xfee000:0xff0000]
    assert len(ota) == 8192 and struct.unpack_from('<I', ota)[0] == 1, 'Missing initial OTA slot selection'
    assert len(factory) <= 0x1000000
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.glob('*.bin')}
    manifest = {'target': 'lilygo-t-embed-cc1101',
                'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                'partitions': entries, 'sha256': hashes,
                'hardware_tested': False}
    (folder / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Factory/OTA layout, binary composition and ESP32-S3 target verified')

if __name__ == '__main__':
    verify(sys.argv[1] if len(sys.argv) > 1 else '.pio/build/lilygo-t-embed-cc1101')
