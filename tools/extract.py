import os
import sys
import argparse
import struct
import hashlib
import glob
import json
import math
import tempfile
from dataclasses import dataclass, asdict
from typing import List, Tuple, Optional, Dict, Any

SECTOR_SIZE = 2048

@dataclass
class FileRecord:
    path: str
    lsn: int
    size: int
    sector_count: int
    sha256: str = ""
    is_iso_file: bool = True
    level_id: Optional[int] = None
    component: Optional[str] = None
    priority: int = 20

def normalize_path(p: str) -> str:
    return p.replace('\\', '/').strip('/')

def atomic_write(dest_path: str, data_generator) -> None:
    parent = os.path.dirname(dest_path)
    if parent:
        os.makedirs(parent, exist_ok=True)
    fd, tmp_path = tempfile.mkstemp(prefix=os.path.basename(dest_path) + ".", suffix=".tmp", dir=parent or ".")
    os.close(fd)
    try:
        with open(tmp_path, 'wb') as out:
            data_generator(out)
            out.flush()
            os.fsync(out.fileno())
        os.replace(tmp_path, dest_path)
    except Exception:
        if os.path.exists(tmp_path):
            try:
                os.remove(tmp_path)
            except OSError:
                pass
        raise

class ISO9660:
    def __init__(self, path: str):
        self.path = path
        self.f = open(path, 'rb')
        self.f.seek(0, os.SEEK_END)
        self.iso_size = self.f.tell()
        self.total_sectors = self.iso_size // SECTOR_SIZE
        self.f.seek(0)

    def close(self):
        if not self.f.closed:
            self.f.close()

    def parse_pvd(self) -> Tuple[int, int]:
        if self.iso_size < 17 * SECTOR_SIZE:
            raise ValueError("ISO image too small (less than 17 sectors)")
        self.f.seek(16 * SECTOR_SIZE)
        pvd = self.f.read(SECTOR_SIZE)
        if len(pvd) < SECTOR_SIZE or pvd[1:6] != b'CD001':
            raise ValueError("Not a valid ISO9660 image (PVD magic 'CD001' not found at sector 16)")

        root_dir_record = pvd[156:156+34]
        extent_lsn = struct.unpack_from('<I', root_dir_record, 2)[0]
        size = struct.unpack_from('<I', root_dir_record, 10)[0]

        if extent_lsn >= self.total_sectors:
            raise ValueError(f"Root directory extent LSN {extent_lsn} out of ISO bounds ({self.total_sectors} sectors)")
        if (extent_lsn * SECTOR_SIZE) + size > self.iso_size:
            raise ValueError(f"Root directory size {size} extends past ISO boundary ({self.iso_size} bytes)")

        return extent_lsn, size

    def _walk_dir(self, lsn: int, size: int, current_path: str, records: List[FileRecord], depth: int = 0):
        if depth > 16:
            raise ValueError("Directory tree too deep (>16), possible circular structure or corruption")
        if lsn + math.ceil(size / SECTOR_SIZE) > self.total_sectors:
            raise ValueError(f"Directory LSN {lsn} (size {size}) exceeds total sectors {self.total_sectors}")

        self.f.seek(lsn * SECTOR_SIZE)
        data = self.f.read(size)
        if len(data) < size:
            raise ValueError(f"Truncated directory read at LSN {lsn}: expected {size} bytes, got {len(data)}")

        offset = 0
        while offset < size:
            length = data[offset]
            if length == 0:
                sector_offset = offset % SECTOR_SIZE
                if sector_offset != 0:
                    offset += (SECTOR_SIZE - sector_offset)
                else:
                    break
                continue

            if offset + length > size:
                raise ValueError(f"Directory record at offset {offset} (length {length}) exceeds directory size {size}")
            if length < 33:
                raise ValueError(f"Invalid directory record length {length} (< 33) at offset {offset}")

            record = data[offset:offset+length]
            extent_lsn = struct.unpack_from('<I', record, 2)[0]
            extent_size = struct.unpack_from('<I', record, 10)[0]
            flags = record[25]
            name_len = record[32]
            
            if 33 + name_len > length:
                raise ValueError(f"Filename length {name_len} exceeds record length {length} at offset {offset}")

            name_bytes = record[33:33+name_len]
            offset += length

            if name_bytes == b'\x00' or name_bytes == b'\x01':
                continue
                
            name = name_bytes.decode('ascii', errors='ignore')
            if ';' in name:
                name = name.split(';')[0]

            is_dir = bool(flags & 0x02)
            rel_path = normalize_path(f"{current_path}/{name}" if current_path else name)

            if is_dir:
                self._walk_dir(extent_lsn, extent_size, rel_path, records, depth + 1)
            else:
                sect_cnt = math.ceil(extent_size / SECTOR_SIZE)
                records.append(FileRecord(
                    path=rel_path,
                    lsn=extent_lsn,
                    size=extent_size,
                    sector_count=sect_cnt,
                    is_iso_file=True,
                    priority=20
                ))

    def records(self) -> List[FileRecord]:
        root_lsn, root_size = self.parse_pvd()
        recs: List[FileRecord] = []
        self._walk_dir(root_lsn, root_size, "", recs)
        return recs

    def copy(self, record: FileRecord, dest_path: str):
        self.copy_range(record.lsn, record.size, dest_path)

    def copy_range(self, lsn: int, size: int, dest_path: str):
        if lsn < 0 or lsn >= self.total_sectors:
            raise ValueError(f"copy_range LSN {lsn} out of ISO bounds ({self.total_sectors} sectors)")
        if lsn * SECTOR_SIZE + size > self.iso_size:
            raise ValueError(f"copy_range LSN {lsn} with size {size} extends past ISO boundary ({self.iso_size} bytes)")

        def writer(out_f):
            self.f.seek(lsn * SECTOR_SIZE)
            bytes_left = size
            while bytes_left > 0:
                chunk = min(bytes_left, 1024 * 1024)
                buf = self.f.read(chunk)
                if not buf:
                    raise IOError(f"Unexpected EOF while reading {size} bytes from LSN {lsn}")
                out_f.write(buf)
                bytes_left -= len(buf)

        atomic_write(dest_path, writer)

def parse_rc1_toc(iso: ISO9660) -> List[Tuple[int, int, int, List[Tuple[str, int, int]]]]:
    toc_lsn = 1500
    if toc_lsn >= iso.total_sectors:
        raise ValueError(f"R&C1 TOC LSN {toc_lsn} out of ISO bounds ({iso.total_sectors} sectors)")

    iso.f.seek(toc_lsn * SECTOR_SIZE)
    toc_header = iso.f.read(8)
    if len(toc_header) < 8:
        raise ValueError("Truncated ISO read when checking R&C1 TOC header")

    magic, toc_size = struct.unpack('<II', toc_header)
    if magic != 1:
        return []

    if toc_size < 8:
        raise ValueError(f"Invalid R&C1 TOC size {toc_size} (< 8)")
    if toc_size % 8 != 0:
        raise ValueError(f"Invalid R&C1 TOC size {toc_size}: entries are not 8-byte aligned")
    if (toc_lsn * SECTOR_SIZE) + toc_size > iso.iso_size:
        raise ValueError(f"R&C1 TOC size {toc_size} extends past ISO EOF boundary ({iso.iso_size} bytes)")

    iso.f.seek(toc_lsn * SECTOR_SIZE)
    toc_data = iso.f.read(toc_size)
    if len(toc_data) < toc_size:
        raise ValueError(f"Truncated read for TOC data: expected {toc_size}, got {len(toc_data)}")

    levels = []

    for i in range(8, toc_size, 8):
        if i + 8 > toc_size:
            break
        header_lsn, header_sectors = struct.unpack_from('<II', toc_data, i)
        if header_lsn == 0:
            continue

        if header_lsn >= iso.total_sectors:
            raise ValueError(f"TOC entry LSN {header_lsn} out of ISO bounds ({iso.total_sectors} sectors)")

        iso.f.seek(header_lsn * SECTOR_SIZE)
        level_header = iso.f.read(0x2434)
        if len(level_header) < 0x2434:
            raise ValueError(f"Truncated TOC entry payload at LSN {header_lsn}")

        check = struct.unpack_from('<I', level_header, 4)[0]
        if check != 0x2434:
            # The disc TOC also contains non-level ranges. Only entries with
            # the R&C1 level-header marker participate in level extraction.
            continue

        level_id = struct.unpack_from('<i', level_header, 0)[0]
        if level_id < 0 or level_id > 18:
            raise ValueError(f"Level ID {level_id} out of R&C1 range [0, 18]")

        files = []
        # data.bin
        start, count = struct.unpack_from('<II', level_header, 8)
        if count > 0:
            if start + count > iso.total_sectors:
                raise ValueError(f"Level {level_id} data.bin range [{start}, {start+count}) exceeds ISO bounds")
            files.append(('data.bin', start, count))

        # gameplay_ntsc.bin
        start, count = struct.unpack_from('<II', level_header, 16)
        if count > 0:
            if start + count > iso.total_sectors:
                raise ValueError(f"Level {level_id} gameplay_ntsc.bin range [{start}, {start+count}) exceeds ISO bounds")
            files.append(('gameplay_ntsc.bin', start, count))

        # gameplay_pal.bin
        start, count = struct.unpack_from('<II', level_header, 24)
        if count > 0:
            if start + count > iso.total_sectors:
                raise ValueError(f"Level {level_id} gameplay_pal.bin range [{start}, {start+count}) exceeds ISO bounds")
            files.append(('gameplay_pal.bin', start, count))

        # occlusion.bin
        start, count = struct.unpack_from('<II', level_header, 32)
        if count > 0:
            if start + count > iso.total_sectors:
                raise ValueError(f"Level {level_id} occlusion.bin range [{start}, {start+count}) exceeds ISO bounds")
            files.append(('occlusion.bin', start, count))

        if not files:
            continue

        low = min(f[1] for f in files)
        high = max(f[1] + f[2] for f in files)

        # Validate that sub-ranges do not overlap and lie strictly inside parent level range [low, high)
        sorted_files = sorted(files, key=lambda f: f[1])
        prev_end = low
        for name, start, count in sorted_files:
            if start < prev_end:
                raise ValueError(f"Level {level_id} sub-range {name} [{start}, {start+count}) overlaps previous range end {prev_end}")
            if start < low or (start + count) > high:
                raise ValueError(f"Level {level_id} sub-range {name} [{start}, {start+count}) is outside parent range [{low}, {high})")
            prev_end = start + count

        levels.append((level_id, low, high, files))

    level_ids = [level[0] for level in levels]
    if len(level_ids) != len(set(level_ids)):
        raise ValueError("R&C1 TOC contains duplicate level IDs")
    if sorted(level_ids) != list(range(19)):
        raise ValueError(f"R&C1 TOC must contain level IDs 0 through 18; found {sorted(level_ids)}")
    return levels

def parse_elf(path: str) -> Tuple[int, int, List[Tuple[int, int, int]]]:
    with open(path, 'rb') as f:
        header = f.read(52)
        if len(header) < 52:
            raise ValueError("File too short to be a valid ELF")
        if header[0:4] != b'\x7fELF':
            raise ValueError("Not an ELF file (magic check failed)")
        if header[4] != 1:
            raise ValueError("Not 32-bit ELF")
        if header[5] != 1:
            raise ValueError("Not little-endian ELF")
        machine = struct.unpack_from('<H', header, 18)[0]
        if machine != 8:
            raise ValueError(f"Not MIPS architecture (machine type {machine} != 8)")

        entry_point = struct.unpack_from('<I', header, 24)[0]
        phoff = struct.unpack_from('<I', header, 28)[0]
        phentsize = struct.unpack_from('<H', header, 42)[0]
        phnum = struct.unpack_from('<H', header, 44)[0]

        segments = []
        f.seek(phoff)
        for _ in range(phnum):
            ph = f.read(phentsize)
            if len(ph) < phentsize:
                raise ValueError("Truncated program header table entry")
            p_type = struct.unpack_from('<I', ph, 0)[0]
            if p_type == 1:  # PT_LOAD
                p_vaddr = struct.unpack_from('<I', ph, 8)[0]
                p_filesz = struct.unpack_from('<I', ph, 16)[0]
                p_memsz = struct.unpack_from('<I', ph, 20)[0]
                segments.append((p_vaddr, p_filesz, p_memsz))

        return entry_point, phnum, segments

def locate_iso(requested: Optional[str], root: str) -> Optional[str]:
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

    extracted_records: List[FileRecord] = []

    for r in records:
        if args.all or r.path in targets:
            dest = os.path.join(raw_dir, r.path)
            print(f"Extracting ISO file {r.path} -> {dest}")
            iso.copy(r, dest)

            # Verify size matches
            actual_size = os.path.getsize(dest)
            if actual_size != r.size:
                raise ValueError(f"Extracted size mismatch for {r.path}: expected {r.size}, got {actual_size}")

            sha = sha256_file(dest)
            r.sha256 = sha
            extracted_records.append(r)

            if r.path == 'SCUS_971.99':
                boot_elf_path = dest
                boot_elf_lsn = r.lsn
                boot_elf_size = r.size

    region = "NTSC-U" if os.path.basename(boot_elf_path).upper() == "SCUS_971.99" else "UNKNOWN"

    if args.all:
        print("Parsing R&C1 TOC...")
        levels = parse_rc1_toc(iso)
        print(f"Discovered {len(levels)} levels (0 through {len(levels)-1 if levels else 0})")

        for lvl_id, low, high, files in levels:
            lvl_dir = os.path.join(raw_dir, 'levels', str(lvl_id))
            wad_path = os.path.join(lvl_dir, 'level.wad')
            total_sectors = high - low
            total_bytes = total_sectors * SECTOR_SIZE
            print(f"Extracting level {lvl_id} ({low} to {high}, {total_sectors} sectors) -> {wad_path}")

            iso.copy_range(low, total_bytes, wad_path)

            wad_actual_size = os.path.getsize(wad_path)
            if wad_actual_size != total_bytes:
                raise ValueError(f"Extracted size mismatch for level {lvl_id} wad: expected {total_bytes}, got {wad_actual_size}")

            wad_sha = sha256_file(wad_path)
            extracted_records.append(FileRecord(
                path=normalize_path(f"levels/{lvl_id}/level.wad"),
                lsn=low,
                size=total_bytes,
                sector_count=total_sectors,
                sha256=wad_sha,
                is_iso_file=False,
                level_id=lvl_id,
                component='level.wad',
                priority=10
            ))

            for name, start, count in files:
                subrange_path = os.path.join(lvl_dir, name)
                sub_bytes = count * SECTOR_SIZE
                print(f"  Extracting {name} (start={start}, count={count}) -> {subrange_path}")
                iso.copy_range(start, sub_bytes, subrange_path)

                sub_actual_size = os.path.getsize(subrange_path)
                if sub_actual_size != sub_bytes:
                    raise ValueError(f"Extracted size mismatch for {name}: expected {sub_bytes}, got {sub_actual_size}")

                sub_sha = sha256_file(subrange_path)
                extracted_records.append(FileRecord(
                    path=normalize_path(f"levels/{lvl_id}/{name}"),
                    lsn=start,
                    size=sub_bytes,
                    sector_count=count,
                    sha256=sub_sha,
                    is_iso_file=False,
                    level_id=lvl_id,
                    component=name,
                    priority=30
                ))

    iso.close()

    # Sort records deterministically by priority (descending), LSN, then path
    extracted_records.sort(key=lambda r: (-r.priority, r.lsn, r.path))

    ep, phnum, segs = (0, 0, [])
    boot_elf_sha = ""
    if boot_elf_path:
        print(f"Parsing ELF: {boot_elf_path}")
        ep, phnum, segs = parse_elf(boot_elf_path)
        boot_elf_sha = sha256_file(boot_elf_path)

    # 1. Write structured JSON manifest
    manifest_data = {
        "iso_path": normalize_path(iso_path),
        "sector_size": SECTOR_SIZE,
        "total_sectors": iso.total_sectors,
        "region": region,
        "boot_elf": {
            "path": "SCUS_971.99",
            "lsn": boot_elf_lsn,
            "size": boot_elf_size,
            "sha256": boot_elf_sha,
            "entry_point": f"0x{ep:08x}",
            "segments": [{"vaddr": f"0x{va:08x}", "filesz": fs, "memsz": ms} for (va, fs, ms) in segs]
        },
        "files": [asdict(r) for r in extracted_records]
    }

    json_manifest_path = os.path.join(out_dir, 'manifest.json')
    def json_writer(f):
        f.write(json.dumps(manifest_data, indent=2).encode('utf-8'))
    atomic_write(json_manifest_path, json_writer)
    print(f"Wrote structured JSON manifest: {json_manifest_path}")

    # 2. Write text manifest for backwards compatibility
    txt_manifest_path = os.path.join(out_dir, 'manifest.txt')
    def txt_writer(f):
        lines = []
        lines.append(f"ISO_PATH={normalize_path(iso_path)}")
        lines.append(f"SECTOR_SIZE={SECTOR_SIZE}")
        lines.append(f"TOTAL_SECTORS={iso.total_sectors}")
        lines.append(f"REGION={region}")
        lines.append(f"BOOT_ELF=SCUS_971.99,{boot_elf_lsn},{boot_elf_size},{boot_elf_sha}")
        lines.append(f"ENTRY_POINT=0x{ep:08x}")
        lines.append(f"SEGMENTS={len(segs)}")
        for i, (va, fs, ms) in enumerate(segs):
            lines.append(f"SEG_{i}=0x{va:08x},{fs},{ms}")
        lines.append("---FILES---")
        for r in extracted_records:
            lines.append(f"{r.path},{r.lsn},{r.size},{r.sector_count},{r.sha256}")
        f.write("\n".join(lines).encode('utf-8'))
    atomic_write(txt_manifest_path, txt_writer)
    print(f"Wrote text manifest: {txt_manifest_path}")

def do_verify(args):
    data_dir = args.data
    raw_dir = os.path.join(data_dir, 'raw')
    json_path = os.path.join(data_dir, 'manifest.json')
    txt_path = os.path.join(data_dir, 'manifest.txt')

    files_to_check = []

    if os.path.exists(json_path):
        print(f"Loading manifest: {json_path}")
        with open(json_path, 'r', encoding='utf-8') as f:
            m = json.load(f)
        for entry in m.get("files", []):
            size = entry.get("size_bytes", entry.get("size", 0))
            files_to_check.append((entry["path"], entry["lsn"], size, entry["sector_count"], entry.get("sha256", ""), entry.get("is_iso_file", True)))
    elif os.path.exists(txt_path):
        print(f"Loading manifest: {txt_path}")
        with open(txt_path, 'r', encoding='utf-8') as f:
            in_files = False
            for line in f:
                line = line.strip()
                if line == "---FILES---":
                    in_files = True
                    continue
                if in_files and line:
                    parts = line.split(',')
                    path = parts[0]
                    lsn = int(parts[1])
                    size = int(parts[2])
                    sectors = int(parts[3]) if len(parts) > 3 else math.ceil(size / SECTOR_SIZE)
                    sha = parts[4] if len(parts) > 4 else ""
                    files_to_check.append((path, lsn, size, sectors, sha, not path.startswith("levels/")))
    else:
        print(f"Error: Manifest not found in {data_dir}")
        sys.exit(1)

    print(f"Verifying {len(files_to_check)} extracted files in {raw_dir}...")
    failures = 0
    verified_count = 0

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    iso_path = locate_iso(args.iso, root)
    iso = ISO9660(iso_path) if iso_path else None
    if iso:
        print(f"Also verifying against raw ISO sector data: {iso_path}")

    for rel_path, lsn, expected_size, sector_count, expected_sha, is_iso_file in files_to_check:
        full_path = os.path.join(raw_dir, rel_path)
        if not os.path.exists(full_path):
            print(f"FAIL: File missing: {full_path}")
            failures += 1
            continue

        actual_size = os.path.getsize(full_path)
        if is_iso_file:
            if actual_size != expected_size:
                print(f"FAIL: Size mismatch for ISO file {rel_path}: expected {expected_size}, got {actual_size}")
                failures += 1
                continue
        else:
            expected_exact = sector_count * SECTOR_SIZE
            if actual_size != expected_exact:
                print(f"FAIL: Exact sector size mismatch for hidden component {rel_path}: expected {expected_exact}, got {actual_size}")
                failures += 1
                continue

        actual_sha = sha256_file(full_path)
        if not expected_sha:
            print(f"FAIL: Manifest has no SHA256 for {rel_path}")
            failures += 1
            continue
        if actual_sha != expected_sha:
            print(f"FAIL: SHA256 mismatch for {rel_path}:\n  expected {expected_sha}\n  got      {actual_sha}")
            failures += 1
            continue

        if iso:
            iso.f.seek(lsn * SECTOR_SIZE)
            iso_data = iso.f.read(actual_size)
            with open(full_path, 'rb') as f:
                disk_data = f.read()
            if iso_data != disk_data:
                print(f"FAIL: Byte-for-byte mismatch against ISO sectors for {rel_path} at LSN {lsn}")
                failures += 1
                continue

        verified_count += 1

    if iso:
        iso.close()

    if failures == 0:
        print(f"SUCCESS: Verified all {verified_count} extracted files perfectly.")
    else:
        print(f"FAILURE: {failures} files failed verification.")
        sys.exit(1)

def do_toc(args):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    iso_path = locate_iso(args.iso, root)
    if not iso_path:
        print("Error: Could not locate ISO file.")
        sys.exit(1)

    iso = ISO9660(iso_path)
    levels = parse_rc1_toc(iso)
    print(f"Parsed {len(levels)} levels from ISO {iso_path}:")
    for lvl_id, low, high, files in levels:
        print(f"Level {lvl_id:2}: Sectors {low:7} to {high:7} ({(high-low)*2048/1024/1024:5.2f} MB)")
        for name, start, count in files:
            print(f"  {name:20} start={start:7} count={count:6}")
    iso.close()

def do_selftest(args):
    print("Running self-test suite (including corruption & validation tests)...")

    # 1. Synthetic ELF test
    header = bytearray(52)
    header[0:4] = b'\x7fELF'
    header[4] = 1  # 32-bit
    header[5] = 1  # LE
    struct.pack_into('<H', header, 18, 8)  # MIPS
    struct.pack_into('<I', header, 24, 0x12345678)  # Entry
    struct.pack_into('<I', header, 28, 52)  # phoff
    struct.pack_into('<H', header, 42, 32)  # phentsize
    struct.pack_into('<H', header, 44, 1)  # phnum

    ph = bytearray(32)
    struct.pack_into('<I', ph, 0, 1)  # PT_LOAD
    struct.pack_into('<I', ph, 8, 0x100000)  # vaddr
    struct.pack_into('<I', ph, 16, 1024)  # filesz
    struct.pack_into('<I', ph, 20, 2048)  # memsz

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
        if os.path.exists(tmp_elf): os.unlink(tmp_elf)

    # 2. Synthetic ISO test
    iso_data = bytearray(1600 * SECTOR_SIZE)
    # PVD at sector 16
    iso_data[16*SECTOR_SIZE+1:16*SECTOR_SIZE+6] = b'CD001'
    struct.pack_into('<I', iso_data, 16*SECTOR_SIZE+156+2, 20)  # root extent LSN
    struct.pack_into('<I', iso_data, 16*SECTOR_SIZE+156+10, 2048)  # size

    # Root dir at sector 20
    rec = bytearray(34)
    rec[0] = 34
    rec[25] = 2  # Directory
    rec[32] = 1
    rec[33] = 0  # Self
    struct.pack_into('<I', rec, 2, 20)
    struct.pack_into('<I', rec, 10, 2048)
    iso_data[20*SECTOR_SIZE:20*SECTOR_SIZE+34] = rec

    rec2 = bytearray(38)
    rec2[0] = 38
    rec2[25] = 0  # File
    rec2[32] = 4
    rec2[33:37] = b'TEST'
    struct.pack_into('<I', rec2, 2, 30)
    struct.pack_into('<I', rec2, 10, 10)
    iso_data[20*SECTOR_SIZE+34:20*SECTOR_SIZE+34+38] = rec2

    # File content at sector 30
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
            if os.path.exists(tmp_out): os.unlink(tmp_out)

        iso.close()
    finally:
        if os.path.exists(tmp_iso): os.unlink(tmp_iso)

    # 3. Corruption Tests
    print("Testing corruption handling...")

    # Corruption test 1: Invalid PVD magic
    bad_pvd_iso = bytearray(1600 * SECTOR_SIZE)
    bad_pvd_iso[16*SECTOR_SIZE+1:16*SECTOR_SIZE+6] = b'BAD01'
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(bad_pvd_iso)
        tmp_bad = f.name
    try:
        iso = ISO9660(tmp_bad)
        try:
            iso.parse_pvd()
            assert False, "Should have raised ValueError on invalid PVD magic"
        except ValueError as e:
            assert "PVD magic 'CD001' not found" in str(e)
            print("Corruption Test (Invalid PVD): PASS")
        iso.close()
    finally:
        if os.path.exists(tmp_bad): os.unlink(tmp_bad)

    # Corruption test 2: Truncated directory record
    bad_dir_iso = bytearray(1600 * SECTOR_SIZE)
    bad_dir_iso[16*SECTOR_SIZE+1:16*SECTOR_SIZE+6] = b'CD001'
    struct.pack_into('<I', bad_dir_iso, 16*SECTOR_SIZE+156+2, 20)
    struct.pack_into('<I', bad_dir_iso, 16*SECTOR_SIZE+156+10, 2048)
    # Put a corrupt record with filename length extending past record length
    bad_rec = bytearray(34)
    bad_rec[0] = 34   # Record length 34
    bad_rec[32] = 10  # Filename length 10 (33 + 10 = 43 > 34 -> error!)
    bad_dir_iso[20*SECTOR_SIZE:20*SECTOR_SIZE+34] = bad_rec
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(bad_dir_iso)
        tmp_bad_dir = f.name
    try:
        iso = ISO9660(tmp_bad_dir)
        try:
            iso.records()
            assert False, "Should have raised ValueError on truncated directory record"
        except ValueError as e:
            assert "Filename length" in str(e) or "exceeds" in str(e)
            print("Corruption Test (Truncated Directory Record): PASS")
        finally:
            iso.close()
    finally:
        if os.path.exists(tmp_bad_dir): os.unlink(tmp_bad_dir)

    # Corruption test 3: Out-of-range sector in TOC
    bad_toc_iso = bytearray(1600 * SECTOR_SIZE)
    # LSN 1500 TOC header
    struct.pack_into('<II', bad_toc_iso, 1500*SECTOR_SIZE, 1, 16)
    # Level entry pointing to LSN 99999 (out of range)
    struct.pack_into('<II', bad_toc_iso, 1500*SECTOR_SIZE+8, 99999, 10)
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(bad_toc_iso)
        tmp_bad_toc = f.name
    try:
        iso = ISO9660(tmp_bad_toc)
        try:
            parse_rc1_toc(iso)
            assert False, "Should have raised ValueError on out-of-bounds header LSN"
        except ValueError as e:
            assert "out of ISO bounds" in str(e)
            print("Corruption Test (Out-of-range Sector): PASS")
        finally:
            iso.close()
    finally:
        if os.path.exists(tmp_bad_toc): os.unlink(tmp_bad_toc)

    # Corruption test 4: Overlapping sub-ranges in level header
    bad_sub_iso = bytearray(2000 * SECTOR_SIZE)
    struct.pack_into('<II', bad_sub_iso, 1500*SECTOR_SIZE, 1, 16)
    struct.pack_into('<II', bad_sub_iso, 1500*SECTOR_SIZE+8, 1600, 10)
    # Level header at 1600
    hdr = bytearray(0x2434)
    struct.pack_into('<i', hdr, 0, 0) # Level 0
    struct.pack_into('<I', hdr, 4, 0x2434) # Check magic
    struct.pack_into('<II', hdr, 8, 1600, 30) # data.bin: 1600..1630
    struct.pack_into('<II', hdr, 16, 1620, 20) # gameplay_ntsc.bin: 1620..1640 (overlaps 1620 < 1630!)
    bad_sub_iso[1600*SECTOR_SIZE:1600*SECTOR_SIZE+0x2434] = hdr
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(bad_sub_iso)
        tmp_bad_sub = f.name
    try:
        iso = ISO9660(tmp_bad_sub)
        try:
            parse_rc1_toc(iso)
            assert False, "Should have raised ValueError when sub-ranges overlap"
        except ValueError as e:
            assert "overlaps" in str(e)
            print("Corruption Test (Overlapping Sub-ranges): PASS")
        finally:
            iso.close()
    finally:
        if os.path.exists(tmp_bad_sub): os.unlink(tmp_bad_sub)


    # Corruption test 5: Atomic write cleanup
    test_target = os.path.join(tempfile.gettempdir(), "test_atomic_cleanup.bin")
    if os.path.exists(test_target): os.unlink(test_target)
    try:
        def bad_writer(out_f):
            out_f.write(b"partial")
            raise RuntimeError("Simulated failure during write")
        try:
            atomic_write(test_target, bad_writer)
            assert False, "Should have raised RuntimeError"
        except RuntimeError:
            pass
        assert not os.path.exists(test_target), "Target file should not exist after failed atomic write"
        assert not any(name.startswith("test_atomic_cleanup.bin.") and name.endswith(".tmp")
                       for name in os.listdir(tempfile.gettempdir())), "Temporary file should be cleaned up after failed atomic write"
        print("Corruption Test (Atomic Write Cleanup): PASS")
    finally:
        if os.path.exists(test_target): os.unlink(test_target)

    print("self-test: ALL TESTS PASSED SUCCESSFULLY!")

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
    ver_p.add_argument('--data', default='data', help='Data directory containing manifest')
    ver_p.add_argument('--iso', help='Optional path to ISO for raw sector comparison')

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
