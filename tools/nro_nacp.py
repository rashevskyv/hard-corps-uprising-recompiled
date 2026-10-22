#!/usr/bin/env python3
"""Prints what hbmenu shows for an NRO: name, author and version from its NACP.

    python3 tools/nro_nacp.py out/build/switch/hcu.nro [expected-version]

With an expected version the exit code is 1 when the NACP's display version differs (deploy.sh
refuses the push). Layout: NRO header "NRO0" at 0x10, the NRO size at 0x18; the asset section
("ASET") follows the NRO and holds (offset, size) pairs for the icon, the NACP and the RomFS;
the NACP has 16 (0x200 name, 0x100 author) entries, then the display version at 0x3060.
"""
import struct
import sys


def nacp_of(path):
    data = open(path, 'rb').read()
    if data[0x10:0x14] != b'NRO0':
        raise SystemExit(f'{path}: not an NRO')
    size = struct.unpack_from('<I', data, 0x18)[0]
    if data[size:size + 4] != b'ASET':
        raise SystemExit(f'{path}: no asset section')
    nacp_off, nacp_size = struct.unpack_from('<QQ', data, size + 0x18)
    if nacp_size < 0x4000:
        raise SystemExit(f'{path}: no NACP in the asset section')
    return data[size + nacp_off:size + nacp_off + nacp_size]


def cstr(b):
    return b.split(b'\0', 1)[0].decode('utf-8', 'replace')


nacp = nacp_of(sys.argv[1])
name, author = cstr(nacp[0:0x200]), cstr(nacp[0x200:0x300])
version = cstr(nacp[0x3060:0x3070])
print(f'NACP: "{name}" by {author}, version {version}')
if len(sys.argv) > 2 and sys.argv[2] != version:
    print(f'NACP version {version} != expected {sys.argv[2]}', file=sys.stderr)
    sys.exit(1)
