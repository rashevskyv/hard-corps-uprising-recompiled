#!/usr/bin/env python3
"""Extract files from an Xbox 360 STFS package (LIVE/PIRS/CON) into a folder.

    python3 -I tools/stfs_extract.py <package> <out_dir> [--list]
    python3 -I tools/stfs_extract.py --dlc <package> <user data root>

--dlc installs a marketplace package (DLC, content type 00000002) the way the PC port's launcher does, under the
user data root the ReXGlue content manager reads (<root>/0000000000000000/<title id>/00000002/<content id>/ and
Headers/00000002/<content id>.header): for the Switch build the root is sdmc:/switch/hcu/user on the SD card.

Layout per Free60 "STFS" (volume descriptor at 0x379: size, reserved, block separation, file table block count (2), file table start block (3)),
the data blocks named by the descriptor, L0 hash tables before every 0xAA data blocks.
"""
import os, struct, sys


class Stfs:
    def __init__(self, path):
        self.f = open(path, 'rb')
        self.magic = self.f.read(4)
        if self.magic not in (b'LIVE', b'PIRS', b'CON '):
            raise SystemExit(f'not an STFS package: {self.magic!r}')
        self.f.seek(0x340)
        (self.header_size, self.content_type, self.metadata_version, self.content_size,
         self.media_id, self.version, self.base_version, self.title_id) = struct.unpack(
            '>IIIQIIII', self.f.read(36))
        self.f.seek(0x379)
        vd = self.f.read(0x24)
        self.block_separation = vd[2]
        self.file_table_blocks = struct.unpack('<H', vd[3:5])[0]
        self.file_table_start = int.from_bytes(vd[5:8], 'little')
        self.total_blocks = struct.unpack('>I', vd[0x1C:0x20])[0]
        self.base = (self.header_size + 0xFFF) & 0xF000
        # Hash tables are doubled unless the package says blocks are not separated.
        self.shift = 0 if (self.base == 0xB000 or (self.block_separation & 1)) else 1

    # Data block number -> raw block index (hash table blocks counted in).
    def raw_block(self, n):
        r = n + (((n + 0xAA) // 0xAA) << self.shift)
        if n >= 0xAA:
            r += ((n + 0x70E4) // 0x70E4) << self.shift
        if n >= 0x70E4:
            r += 1 << self.shift
        return r

    def block_offset(self, n):
        return self.base + (self.raw_block(n) << 12)

    def hash_entry_offset(self, n):
        # The L0 table of a group sits right before the group's first data block.
        table = self.raw_block((n // 0xAA) * 0xAA) - (1 << self.shift)
        return self.base + (table << 12) + (n % 0xAA) * 0x18

    def next_block(self, n):
        self.f.seek(self.hash_entry_offset(n) + 0x15)
        return int.from_bytes(self.f.read(3), 'big')

    def read_block(self, n):
        self.f.seek(self.block_offset(n))
        return self.f.read(0x1000)

    def entries(self):
        raw = b''
        block = self.file_table_start
        for _ in range(self.file_table_blocks):
            raw += self.read_block(block)
            block = self.next_block(block)
        out = []
        for i in range(len(raw) // 64):
            e = raw[i * 64:(i + 1) * 64]
            flags = e[0x28]
            name_len = flags & 0x3F
            if name_len == 0:
                continue
            out.append({
                'name': e[:name_len].decode('latin-1'),
                'dir': bool(flags & 0x80),
                'contiguous': bool(flags & 0x40),
                'blocks': int.from_bytes(e[0x29:0x2C], 'little'),
                'start': int.from_bytes(e[0x2F:0x32], 'little'),
                'parent': struct.unpack('>h', e[0x32:0x34])[0],
                'size': struct.unpack('>I', e[0x34:0x38])[0],
            })
        return out

    def path_of(self, entries, i):
        parts = []
        while i >= 0:
            parts.append(entries[i]['name'])
            i = entries[i]['parent']
        return '/'.join(reversed(parts))

    def read_file(self, e):
        data = bytearray()
        block = e['start']
        for _ in range(e['blocks']):
            data += self.read_block(block)
            block = block + 1 if e['contiguous'] else self.next_block(block)
        return bytes(data[:e['size']])


# The STFS header fields of a marketplace package (same offsets as the PC launcher's src/stfs.cpp).
CONTENT_ID_OFFSET, LICENSE_OFFSET, LICENSE_ENTRIES, DISPLAY_NAME_OFFSET, DISPLAY_NAME_BYTES = 0x32C, 0x22C, 16, 0x411, 0x100
DLC_CONTENT_TYPE = 2
# The .header the content manager reads: XCONTENT_AGGREGATE_DATA (0x148 bytes, big-endian), then the licence mask (LE32).
HEADER_BYTES, DISPLAY_NAME_AT, FILE_NAME_AT, FILE_NAME_BYTES, TITLE_AT, HDD_DEVICE_ID = 0x148, 8, 264, 42, 0x140, 1


def content_header(content_id, display_name, title_id, license_mask):
    h = bytearray(HEADER_BYTES)
    h[0:4] = HDD_DEVICE_ID.to_bytes(4, 'big')
    h[4:8] = DLC_CONTENT_TYPE.to_bytes(4, 'big')
    name = display_name[:127].encode('utf-16-be')
    h[DISPLAY_NAME_AT:DISPLAY_NAME_AT + len(name)] = name
    fname = content_id.encode('ascii')[:FILE_NAME_BYTES]
    h[FILE_NAME_AT:FILE_NAME_AT + len(fname)] = fname
    h[TITLE_AT:TITLE_AT + 4] = title_id.to_bytes(4, 'big')
    if license_mask:
        h += license_mask.to_bytes(4, 'little')
    return bytes(h)


def install_dlc(pkg, root):
    s = Stfs(pkg)
    if s.content_type != DLC_CONTENT_TYPE:
        raise SystemExit(f'not a marketplace (DLC) package: content type {s.content_type:08X}')
    s.f.seek(CONTENT_ID_OFFSET)
    content_id = s.f.read(20).hex().upper()
    s.f.seek(DISPLAY_NAME_OFFSET)
    display_name = s.f.read(DISPLAY_NAME_BYTES).decode('utf-16-be', 'replace').split(chr(0))[0]
    s.f.seek(LICENSE_OFFSET)
    lic = s.f.read(LICENSE_ENTRIES * 16)
    license_mask = 0
    for i in range(LICENSE_ENTRIES):
        e = lic[i * 16:(i + 1) * 16]
        if int.from_bytes(e[12:16], 'big'):
            license_mask |= int.from_bytes(e[8:12], 'big')
    title = f'{s.title_id:08X}'
    out = os.path.join(root, '0000000000000000', title, f'{DLC_CONTENT_TYPE:08X}', content_id)
    header = os.path.join(root, '0000000000000000', title, 'Headers', f'{DLC_CONTENT_TYPE:08X}', content_id + '.header')
    print(f'{display_name!r} title {title} content id {content_id} licence mask {license_mask:08X} -> {out}')
    entries = s.entries()
    n = 0
    for i, e in enumerate(entries):
        path = s.path_of(entries, i)
        if '..' in path.split('/'):
            raise SystemExit(f'bad entry name: {path}')
        dst = os.path.join(out, path)
        if e['dir']:
            os.makedirs(dst, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'wb') as w:
            w.write(s.read_file(e))
        n += 1
    os.makedirs(os.path.dirname(header), exist_ok=True)
    with open(header, 'wb') as w:
        w.write(content_header(content_id, display_name, s.title_id, license_mask))
    print(f'{n} files, header {header}')


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    if sys.argv[1] == '--dlc':
        if len(sys.argv) != 4:
            raise SystemExit(__doc__)
        install_dlc(sys.argv[2], sys.argv[3])
        return
    pkg, out = sys.argv[1], sys.argv[2]
    s = Stfs(pkg)
    print(f'{s.magic.decode()} title {s.title_id:08X} type {s.content_type:X} version '
          f'{s.version:X} blocks {s.total_blocks} base {s.base:X} shift {s.shift}')
    entries = s.entries()
    total = 0
    for i, e in enumerate(entries):
        path = s.path_of(entries, i)
        if e['dir']:
            print(f'  {path}/')
            continue
        total += e['size']
        print(f'  {path}  {e["size"]} bytes, {e["blocks"]} blocks, start {e["start"]}'
              f'{" contiguous" if e["contiguous"] else ""}')
        if '--list' in sys.argv:
            continue
        dst = os.path.join(out, path)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'wb') as w:
            w.write(s.read_file(e))
    print(f'{len(entries)} entries, {total} bytes')


if __name__ == '__main__':
    main()
