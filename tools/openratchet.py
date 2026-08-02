#!/usr/bin/env python3
"""Small, deterministic PS2 image extractor for OpenRatchet.

It indexes the ISO9660 filesystem and extracts only the boot files by default.
The ISO stays the source of truth; generated data belongs in data/.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

SECTOR_SIZE = 2048
BOOT_ELF = "SCUS_971.99"
RAC1_TOC_LSN = 1500
RAC1_TOC_MAX_BYTES = 0x200000
RAC1_HEADER_BYTES = 0x2434


@dataclass(frozen=True)
class FileRecord:
    path: str
    lsn: int
    size: int


class ISO9660:
    def __init__(self, path: Path):
        self.path = path
        self.fp = path.open("rb")

    def close(self) -> None:
        self.fp.close()

    def __enter__(self) -> "ISO9660":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def read(self, offset: int, size: int) -> bytes:
        self.fp.seek(offset)
        data = self.fp.read(size)
        if len(data) != size:
            raise ValueError(f"short ISO read at 0x{offset:X}: {len(data)} != {size}")
        return data

    def sector(self, lsn: int, count: int = 1) -> bytes:
        return self.read(lsn * SECTOR_SIZE, count * SECTOR_SIZE)

    def records(self) -> list[FileRecord]:
        pvd = self.sector(16)
        if pvd[1:6] != b"CD001" or pvd[0] != 1:
            raise ValueError("ISO does not contain an ISO9660 primary volume descriptor")

        root = pvd[156:190]
        root_size = struct.unpack_from("<I", root, 10)[0]
        root_lsn = struct.unpack_from("<I", root, 2)[0]
        result: list[FileRecord] = []
        self._walk(root_lsn, root_size, "", result, set())
        return result

    def _walk(
        self,
        lsn: int,
        size: int,
        parent: str,
        result: list[FileRecord],
        visited: set[tuple[int, int]],
    ) -> None:
        key = (lsn, size)
        if key in visited:
            return
        visited.add(key)

        data = self.sector(lsn, (size + SECTOR_SIZE - 1) // SECTOR_SIZE)
        offset = 0
        while offset < size:
            length = data[offset]
            if length == 0:
                offset = ((offset // SECTOR_SIZE) + 1) * SECTOR_SIZE
                continue
            if offset + length > len(data) or length < 34:
                raise ValueError(f"invalid directory record at sector {lsn}")

            record = data[offset : offset + length]
            extent = struct.unpack_from("<I", record, 2)[0]
            file_size = struct.unpack_from("<I", record, 10)[0]
            flags = record[25]
            name_len = record[32]
            raw_name = record[33 : 33 + name_len]
            offset += length

            if raw_name in (b"\x00", b"\x01"):
                continue
            name = raw_name.decode("ascii", errors="replace").split(";", 1)[0]
            path = f"{parent}/{name}" if parent else name
            if flags & 2:
                self._walk(extent, file_size, path, result, visited)
            else:
                result.append(FileRecord(path, extent, file_size))

    def copy(self, record: FileRecord, destination: Path) -> None:
        destination.parent.mkdir(parents=True, exist_ok=True)
        remaining = record.size
        self.fp.seek(record.lsn * SECTOR_SIZE)
        with destination.open("wb") as out:
            while remaining:
                chunk = self.fp.read(min(1024 * 1024, remaining))
                if not chunk:
                    raise ValueError(f"short file while extracting {record.path}")
                out.write(chunk)
                remaining -= len(chunk)


def locate_iso(requested: Path | None) -> Path:
    if requested:
        if not requested.is_file():
            raise FileNotFoundError(requested)
        return requested
    candidates = sorted(Path("games").glob("*.iso"))
    if len(candidates) != 1:
        raise FileNotFoundError("pass --iso; expected exactly one ISO in games/")
    return candidates[0]


def parse_elf(path: Path) -> tuple[int, int, list[tuple[int, int, int]]]:
    with path.open("rb") as fp:
        header = fp.read(52)
        if len(header) != 52 or header[:4] != b"\x7fELF":
            raise ValueError(f"{path} is not an ELF32 image")
        if header[4] != 1 or header[5] != 1:
            raise ValueError(f"{path} is not little-endian ELF32")
        machine = struct.unpack_from("<H", header, 18)[0]
        if machine != 8:
            raise ValueError(f"{path} is not a MIPS ELF (machine={machine})")
        entry = struct.unpack_from("<I", header, 24)[0]
        phoff = struct.unpack_from("<I", header, 28)[0]
        phentsize = struct.unpack_from("<H", header, 42)[0]
        phnum = struct.unpack_from("<H", header, 44)[0]
        if phentsize < 32:
            raise ValueError("unsupported ELF program-header size")
        segments = []
        for index in range(phnum):
            fp.seek(phoff + index * phentsize)
            ph = fp.read(phentsize)
            if len(ph) < 32:
                raise ValueError("short ELF program header")
            p_type, p_offset, p_vaddr, _, p_filesz, p_memsz, _, _ = struct.unpack_from("<IIIIIIII", ph)
            if p_type == 1:
                segments.append((p_vaddr, p_filesz, p_memsz))
        if not segments:
            raise ValueError("ELF has no loadable segments")
        return entry, phnum, segments


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fp:
        for chunk in iter(lambda: fp.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def extract(iso_path: Path, out_dir: Path, all_files: bool) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    with ISO9660(iso_path) as iso:
        records = iso.records()
        by_name = {record.path.upper(): record for record in records}
        boot = by_name.get(BOOT_ELF)
        system = by_name.get("SYSTEM.CNF")
        iop = by_name.get("IOPRP243.IMG")
        if not boot:
            raise ValueError(f"{BOOT_ELF} not found in ISO")

        selected = records if all_files else [record for record in (system, boot, iop) if record]
        for record in selected:
            destination = out_dir / "raw" / record.path
            iso.copy(record, destination)

        elf_path = out_dir / "raw" / boot.path
        entry, segment_count, segments = parse_elf(elf_path)
        manifest = out_dir / "manifest.txt"
        with manifest.open("w", encoding="utf-8", newline="\n") as fp:
            fp.write(f"iso={iso_path.resolve()}\n")
            fp.write(f"sector_size={SECTOR_SIZE}\n")
            fp.write(f"boot={boot.path}\n")
            fp.write(f"boot_lsn={boot.lsn}\n")
            fp.write(f"boot_size={boot.size}\n")
            fp.write(f"boot_sha256={sha256(elf_path)}\n")
            fp.write(f"entry=0x{entry:08X}\n")
            fp.write(f"load_segments={segment_count}\n")
            for index, (vaddr, filesz, memsz) in enumerate(segments):
                fp.write(f"segment_{index}=0x{vaddr:08X},{filesz},{memsz}\n")
            fp.write(f"indexed_files={len(records)}\n")
            for record in records:
                fp.write(f"file\t{record.path}\t{record.lsn}\t{record.size}\n")

    print(f"extracted: {elf_path}")
    print(f"manifest:  {manifest}")
    print(f"indexed:   {len(records)} ISO files")
    print(f"entry:     0x{entry:08X}")


def verify(data_dir: Path) -> None:
    manifest = data_dir / "manifest.txt"
    boot = data_dir / "raw" / BOOT_ELF
    if not manifest.is_file() or not boot.is_file():
        raise FileNotFoundError("run extract first: data/manifest.txt and data/raw/SCUS_971.99 are required")
    entry, _, segments = parse_elf(boot)
    expected = next((line.split("=", 1)[1] for line in manifest.read_text().splitlines() if line.startswith("boot_sha256=")), "")
    if expected != sha256(boot):
        raise ValueError("boot ELF hash does not match manifest")
    print(f"verified:  {boot}")
    print(f"entry:     0x{entry:08X}")
    print(f"segments:  {len(segments)}")


def rac1_levels(iso: ISO9660) -> list[tuple[int, int, int]]:
    prefix = iso.read(RAC1_TOC_LSN * SECTOR_SIZE, 8)
    magic, toc_size = struct.unpack("<II", prefix)
    if magic != 1 or not 0 < toc_size <= RAC1_TOC_MAX_BYTES:
        raise ValueError("invalid R&C1 table of contents")

    toc = iso.read(RAC1_TOC_LSN * SECTOR_SIZE, toc_size)
    levels = []
    seen = set()
    for offset in range(8, len(toc) - 7, 8):
        header_lsn, header_sectors = struct.unpack_from("<II", toc, offset)
        if not header_sectors:
            continue
        try:
            header = iso.read(header_lsn * SECTOR_SIZE, RAC1_HEADER_BYTES)
        except ValueError:
            continue
        if struct.unpack_from("<I", header, 4)[0] != RAC1_HEADER_BYTES:
            continue

        ranges = [struct.unpack_from("<II", header, field) for field in (8, 16, 24, 32)]
        ranges = [(start, size) for start, size in ranges if size]
        if not ranges:
            continue
        low = min(start for start, _ in ranges)
        high = max(start + size for start, size in ranges)
        level = (struct.unpack_from("<i", header, 0)[0], low, high)
        if level not in seen:
            seen.add(level)
            levels.append(level)
    return levels


def toc(iso_path: Path) -> None:
    with ISO9660(iso_path) as iso:
        levels = rac1_levels(iso)
    if not levels:
        raise ValueError("R&C1 table of contents contains no level ranges")
    print(f"rac1_toc_lsn: {RAC1_TOC_LSN}")
    print(f"rac1_levels: {len(levels)}")
    for level_id, low, high in levels[:5]:
        print(f"level: id={level_id} lsn={low} sectors={high - low}")


def self_test() -> None:
    sample = bytearray(52 + 32)
    sample[:4] = b"\x7fELF"
    sample[4:7] = bytes((1, 1, 1))
    struct.pack_into("<H", sample, 18, 8)
    struct.pack_into("<I", sample, 24, 0x1000)
    struct.pack_into("<I", sample, 28, 52)
    struct.pack_into("<HH", sample, 42, 32, 1)
    struct.pack_into("<IIIIIIII", sample, 52, 1, 0, 0x1000, 0, 4, 8, 5, 16)
    temp = Path(".openratchet-self-test.elf")
    temp.write_bytes(sample)
    try:
        entry, count, segments = parse_elf(temp)
        assert entry == 0x1000 and count == 1 and segments == [(0x1000, 4, 8)]
    finally:
        temp.unlink(missing_ok=True)
    print("self-test: PASS")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="OpenRatchet ISO extractor")
    sub = parser.add_subparsers(dest="command", required=True)

    p_extract = sub.add_parser("extract", help="index the ISO and extract its boot files")
    p_extract.add_argument("--iso", type=Path)
    p_extract.add_argument("--out", type=Path, default=Path("data"))
    p_extract.add_argument("--all", action="store_true", help="also extract every ISO9660 file")

    p_verify = sub.add_parser("verify", help="verify extracted boot data")
    p_verify.add_argument("--data", type=Path, default=Path("data"))
    p_toc = sub.add_parser("toc", help="validate the R&C1 raw-sector table of contents")
    p_toc.add_argument("--iso", type=Path)
    sub.add_parser("self-test", help="run the extractor self-check")
    args = parser.parse_args(argv)

    try:
        if args.command == "extract":
            extract(locate_iso(args.iso), args.out, args.all)
        elif args.command == "verify":
            verify(args.data)
        elif args.command == "toc":
            toc(locate_iso(args.iso))
        else:
            self_test()
        return 0
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
