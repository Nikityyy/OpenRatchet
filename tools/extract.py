import os
import sys
import argparse
import struct
import hashlib
import glob
from dataclasses import dataclass
from typing import List, Tuple, Optional

SECTOR_SIZE = 2048

@dataclass
class FileRecord:
    path: str
    lsn: int
    size: int

class ISO9660:
    def __init__(self, path: str):
        self.path = path
        self.f = open(path, 'rb')
        
    def close(self):
        self.f.close()

    def parse_pvd(self) -> Tuple[int, int]:
        self.f.seek(16 * SECTOR_SIZE)
        pvd = self.f.read(SECTOR_SIZE)
        if pvd[1:6] != b'CD001':
            raise ValueError("Not a valid ISO9660 image (PVD magic not found at sector 16)")
        
        # Root directory record is at offset 156, length 34
        root_dir_record = pvd[156:156+34]
        extent_lsn = struct.unpack_from('<I', root_dir_record, 2)[0]
        size = struct.unpack_from('<I', root_dir_record, 10)[0]
        return extent_lsn, size

    def _walk_dir(self, lsn: int, size: int, current_path: str, records: List[FileRecord]):
        self.f.seek(lsn * SECTOR_SIZE)
        data = self.f.read(size)
        offset = 0
        
        while offset < size:
            length = data[offset]
            if length == 0:
                # Pad to next sector boundary if we hit a zero length
                sector_offset = offset % SECTOR_SIZE
                if sector_offset != 0:
                    offset += (SECTOR_SIZE - sector_offset)
                else:
                    break
                continue

            if offset + length > size:
                break

            record = data[offset:offset+length]
            extent_lsn = struct.unpack_from('<I', record, 2)[0]
            extent_size = struct.unpack_from('<I', record, 10)[0]
            flags = record[25]
            name_len = record[32]
            name_bytes = record[33:33+name_len]
            
            offset += length

            if name_bytes == b'\x00' or name_bytes == b'\x01':
                continue
                
            name = name_bytes.decode('ascii', errors='ignore')
            if ';' in name:
                name = name.split(';')[0]

            is_dir = bool(flags & 0x02)
            full_path = f"{current_path}/{name}" if current_path else name

            if is_dir:
                self._walk_dir(extent_lsn, extent_size, full_path, records)
            else:
                records.append(FileRecord(full_path, extent_lsn, extent_size))

    def records(self) -> List[FileRecord]:
        root_lsn, root_size = self.parse_pvd()
        recs = []
        self._walk_dir(root_lsn, root_size, "", recs)
        return recs

    def copy(self, record: FileRecord, dest_path: str):
        self.copy_range(record.lsn, record.size, dest_path)

    def copy_range(self, lsn: int, size: int, dest_path: str):
        os.makedirs(os.path.dirname(dest_path), exist_ok=True)
        self.f.seek(lsn * SECTOR_SIZE)
        bytes_left = size
        with open(dest_path, 'wb') as out:
            while bytes_left > 0:
                chunk = min(bytes_left, 1024 * 1024)
                data = self.f.read(chunk)
                if not data:
                    break
                out.write(data)
                bytes_left -= len(data)

def parse_rc1_toc(iso: ISO9660) -> List[Tuple[int, int, int, List[Tuple[str, int, int]]]]:
    # 1.2 Implement R&C1 hidden TOC parsing
    iso.f.seek(1500 * SECTOR_SIZE)
    toc_header = iso.f.read(8)
    magic, toc_size = struct.unpack('<II', toc_header)
    if magic != 1:
        # Not a valid R&C1 TOC
        return []
    
    iso.f.seek(1500 * SECTOR_SIZE)
    toc_data = iso.f.read(toc_size)
    
    levels = []
    
    # Entries start at offset 8, each is 8 bytes
    for i in range(8, toc_size, 8):
        if i + 8 > toc_size:
            break
        header_lsn, header_sectors = struct.unpack_from('<II', toc_data, i)
        if header_lsn == 0:
            continue
            
        iso.f.seek(header_lsn * SECTOR_SIZE)
        level_header = iso.f.read(0x2434)
        if len(level_header) < 0x2434:
            continue
            
        check = struct.unpack_from('<I', level_header, 4)[0]
        if check != 0x2434:
            continue
            
        level_id = struct.unpack_from('<i', level_header, 0)[0]
        
        files = []
        # data.bin
        start, count = struct.unpack_from('<II', level_header, 8)
        if count > 0: files.append(('data.bin', start, count))
        
        # gameplay_ntsc.bin
        start, count = struct.unpack_from('<II', level_header, 16)
        if count > 0: files.append(('gameplay_ntsc.bin', start, count))
        
        # gameplay_pal.bin
        start, count = struct.unpack_from('<II', level_header, 24)
        if count > 0: files.append(('gameplay_pal.bin', start, count))
        
        # occlusion.bin
        start, count = struct.unpack_from('<II', level_header, 32)
        if count > 0: files.append(('occlusion.bin', start, count))
        
        if not files:
            continue
            
        low = min(f[1] for f in files)
        high = max(f[1] + f[2] for f in files)
        levels.append((level_id, low, high, files))
        
    return levels

def parse_elf(path: str) -> Tuple[int, int, List[Tuple[int, int, int]]]:
    # 1.3 Implement ELF parser function
    with open(path, 'rb') as f:
        header = f.read(52)
        if header[0:4] != b'\x7fELF':
            raise ValueError("Not an ELF file")
        if header[4] != 1:
            raise ValueError("Not 32-bit ELF")
        if header[5] != 1:
            raise ValueError("Not little-endian ELF")
        machine = struct.unpack_from('<H', header, 18)[0]
        if machine != 8:
            raise ValueError("Not MIPS architecture")
            
        entry_point = struct.unpack_from('<I', header, 24)[0]
        phoff = struct.unpack_from('<I', header, 28)[0]
        phentsize = struct.unpack_from('<H', header, 42)[0]
        phnum = struct.unpack_from('<H', header, 44)[0]
        
        segments = []
        f.seek(phoff)
        for _ in range(phnum):
            ph = f.read(phentsize)
            p_type = struct.unpack_from('<I', ph, 0)[0]
            if p_type == 1: # PT_LOAD
                p_vaddr = struct.unpack_from('<I', ph, 8)[0]
                p_filesz = struct.unpack_from('<I', ph, 16)[0]
                p_memsz = struct.unpack_from('<I', ph, 20)[0]
                segments.append((p_vaddr, p_filesz, p_memsz))
                
        return entry_point, phnum, segments

def locate_iso(requested: Optional[str], root: str) -> Optional[str]:
    # 1.8 Implement locate_iso
    if requested:
        return requested if os.path.exists(requested) else None
        
    games_dir = os.path.join(root, 'games')
    if os.path.exists(games_dir):
        isos = glob.glob(os.path.join(games_dir, '*.iso'))
        if len(isos) == 1:
            return isos[0]
    return None

def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()

def do_extract(args):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    iso_path = locate_iso(args.iso, root)
    if not iso_path:
        print("Error: Could not locate ISO file. Use --iso or place exactly one .iso in games/")
        sys.exit(1)
        
    print(f"Opening ISO: {iso_path}")
    iso = ISO9660(iso_path)
    records = iso.records()
    
    out_dir = args.out
    raw_dir = os.path.join(out_dir, 'raw')
    os.makedirs(raw_dir, exist_ok=True)
    
    targets = {'SYSTEM.CNF', 'SCUS_971.99', 'IOPRP243.IMG'}
    boot_elf_path = ""
    boot_elf_lsn = 0
    boot_elf_size = 0
    
    manifest_files = []
    
    for r in records:
        if args.all or r.path in targets:
            dest = os.path.join(raw_dir, r.path)
            print(f"Extracting {r.path} -> {dest}")
            iso.copy(r, dest)
            manifest_files.append((r.path, r.lsn, r.size))
            if r.path == 'SCUS_971.99':
                boot_elf_path = dest
                boot_elf_lsn = r.lsn
                boot_elf_size = r.size
                
    if args.all:
        print("Parsing R&C1 TOC...")
        levels = parse_rc1_toc(iso)
        for lvl_id, low, high, files in levels:
            lvl_dir = os.path.join(raw_dir, 'levels', str(lvl_id))
            print(f"Extracting level {lvl_id} ({low} to {high}) -> {lvl_dir}/level.wad")
            total_size = (high - low) * SECTOR_SIZE
            iso.copy_range(low, total_size, os.path.join(lvl_dir, 'level.wad'))
            
            for name, start, count in files:
                rel_start = start - low
                manifest_files.append((f"levels/{lvl_id}/{name}", start, count * SECTOR_SIZE))
                
    iso.close()
    
    if boot_elf_path:
        print(f"Parsing ELF: {boot_elf_path}")
        ep, phnum, segs = parse_elf(boot_elf_path)
        sha = sha256_file(boot_elf_path)
        
        manifest_path = os.path.join(out_dir, 'manifest.txt')
        with open(manifest_path, 'w') as f:
            f.write(f"ISO_PATH={iso_path}\n")
            f.write(f"SECTOR_SIZE={SECTOR_SIZE}\n")
            f.write(f"BOOT_ELF=SCUS_971.99,{boot_elf_lsn},{boot_elf_size},{sha}\n")
            f.write(f"ENTRY_POINT=0x{ep:08x}\n")
            f.write(f"SEGMENTS={len(segs)}\n")
            for i, (va, fs, ms) in enumerate(segs):
                f.write(f"SEG_{i}=0x{va:08x},{fs},{ms}\n")
            f.write("---FILES---\n")
            for path, lsn, size in manifest_files:
                f.write(f"{path},{lsn},{size}\n")
        print(f"Wrote manifest: {manifest_path}")

def do_verify(args):
    manifest_path = os.path.join(args.data, 'manifest.txt')
    if not os.path.exists(manifest_path):
        print(f"Manifest not found: {manifest_path}")
        sys.exit(1)
        
    expected_sha = ""
    with open(manifest_path, 'r') as f:
        for line in f:
            if line.startswith('BOOT_ELF='):
                parts = line.strip().split(',')
                expected_sha = parts[3]
                break
                
    elf_path = os.path.join(args.data, 'raw', 'SCUS_971.99')
    if not os.path.exists(elf_path):
        print(f"ELF not found: {elf_path}")
        sys.exit(1)
        
    actual_sha = sha256_file(elf_path)
    if expected_sha == actual_sha:
        print(f"verified: {elf_path}")
    else:
        print(f"verify failed for {elf_path}: expected {expected_sha}, got {actual_sha}")
        sys.exit(1)

def do_toc(args):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    iso_path = locate_iso(args.iso, root)
    if not iso_path:
        print("Error: Could not locate ISO file.")
        sys.exit(1)
        
    iso = ISO9660(iso_path)
    levels = parse_rc1_toc(iso)
    for lvl_id, low, high, files in levels:
        print(f"Level {lvl_id:2}: Sectors {low:7} to {high:7} ({(high-low)*2048/1024/1024:5.2f} MB)")
        for name, start, count in files:
            print(f"  {name:20} start={start:7} count={count:6}")
    iso.close()

def do_selftest(args):
    # 1.7 Implement self-test command
    import tempfile
    
    print("Running self-test...")
    
    # Synthetic ELF
    header = bytearray(52)
    header[0:4] = b'\x7fELF'
    header[4] = 1 # 32-bit
    header[5] = 1 # LE
    struct.pack_into('<H', header, 18, 8) # MIPS
    struct.pack_into('<I', header, 24, 0x12345678) # Entry
    struct.pack_into('<I', header, 28, 52) # phoff
    struct.pack_into('<H', header, 42, 32) # phentsize
    struct.pack_into('<H', header, 44, 1) # phnum
    
    ph = bytearray(32)
    struct.pack_into('<I', ph, 0, 1) # PT_LOAD
    struct.pack_into('<I', ph, 8, 0x100000) # vaddr
    struct.pack_into('<I', ph, 16, 1024) # filesz
    struct.pack_into('<I', ph, 20, 2048) # memsz
    
    elf_data = header + ph
    
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(elf_data)
        tmp_elf = f.name
        
    try:
        ep, phnum, segs = parse_elf(tmp_elf)
        assert ep == 0x12345678
        assert phnum == 1
        assert segs[0] == (0x100000, 1024, 2048)
        print("parse_elf: PASS")
    finally:
        os.unlink(tmp_elf)
        
    # Synthetic ISO
    iso_data = bytearray(1600 * SECTOR_SIZE)
    # PVD at 16
    iso_data[16*SECTOR_SIZE+1:16*SECTOR_SIZE+6] = b'CD001'
    struct.pack_into('<I', iso_data, 16*SECTOR_SIZE+156+2, 20) # extent LSN
    struct.pack_into('<I', iso_data, 16*SECTOR_SIZE+156+10, 2048) # size
    
    # Root dir at 20
    rec = bytearray(34)
    rec[0] = 34
    rec[25] = 2 # Directory
    rec[32] = 1
    rec[33] = 0 # Self
    struct.pack_into('<I', rec, 2, 20)
    struct.pack_into('<I', rec, 10, 2048)
    iso_data[20*SECTOR_SIZE:20*SECTOR_SIZE+34] = rec
    
    rec2 = bytearray(38)
    rec2[0] = 38
    rec2[25] = 0 # File
    rec2[32] = 4
    rec2[33:37] = b'TEST'
    struct.pack_into('<I', rec2, 2, 30)
    struct.pack_into('<I', rec2, 10, 10)
    iso_data[20*SECTOR_SIZE+34:20*SECTOR_SIZE+34+38] = rec2
    
    # File content at 30
    iso_data[30*SECTOR_SIZE:30*SECTOR_SIZE+10] = b'0123456789'
    
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(iso_data)
        tmp_iso = f.name
        
    try:
        iso = ISO9660(tmp_iso)
        recs = iso.records()
        assert len(recs) == 1
        assert recs[0].path == 'TEST'
        assert recs[0].lsn == 30
        assert recs[0].size == 10
        
        with tempfile.NamedTemporaryFile(delete=False) as out_f:
            tmp_out = out_f.name
        try:
            iso.copy_range(30, 10, tmp_out)
            with open(tmp_out, 'rb') as vf:
                assert vf.read() == b'0123456789'
            print("ISO9660 copy_range: PASS")
        finally:
            os.unlink(tmp_out)
            
        iso.close()
    finally:
        os.unlink(tmp_iso)
        
    print("self-test: PASS")

def main():
    parser = argparse.ArgumentParser(description="OpenRatchet ISO Extraction Tool")
    subparsers = parser.add_subparsers(dest='command', required=True)
    
    # extract
    ext_p = subparsers.add_parser('extract')
    ext_p.add_argument('--iso', help='Path to ISO')
    ext_p.add_argument('--out', default='data', help='Output directory')
    ext_p.add_argument('--all', action='store_true', help='Extract all files')
    
    # verify
    ver_p = subparsers.add_parser('verify')
    ver_p.add_argument('--data', default='data', help='Data directory containing manifest.txt')
    
    # toc
    toc_p = subparsers.add_parser('toc')
    toc_p.add_argument('--iso', help='Path to ISO')
    
    # self-test
    subparsers.add_parser('self-test')
    
    args = parser.parse_args()
    
    if args.command == 'extract':
        do_extract(args)
    elif args.command == 'verify':
        do_verify(args)
    elif args.command == 'toc':
        do_toc(args)
    elif args.command == 'self-test':
        do_selftest(args)

if __name__ == '__main__':
    main()
