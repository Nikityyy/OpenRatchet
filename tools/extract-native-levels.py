#!/usr/bin/env python3
"""Extract R&C1 native level spans without re-extracting the full disc.

Important R&C1 detail: the retail TOC tail contains 19 raw SectorRange words,
but those raw ranges are game metadata, not trustworthy host-file extents. The
actual per-level envelope is identified by the 0x2434-byte amalgamated header.
This tool therefore preserves the raw tail exactly for the game-visible TOC and
separately discovers validated native extraction spans by scanning TOC sector
references the same way the established R&C1 tooling does.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from pathlib import Path

SECTOR_BYTES = 0x800
TOC_SECTOR = 1500
LEVEL_TABLE_OFFSET = 0x28C8
LEVEL_COUNT = 19
RAC1_TOC_BYTES = 0x2960
AMALGAMATED_HEADER_BYTES = 0x2434
CHUNK_BYTES = 8 * 1024 * 1024
MAX_NATIVE_LEVEL_BYTES = 1024 * 1024 * 1024


def read_u32_bytes(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def read_exact(handle, offset: int, size: int) -> bytes:
    handle.seek(offset)
    data = handle.read(size)
    if len(data) != size:
        raise RuntimeError(f"unexpected end of ISO at 0x{offset:x} reading 0x{size:x} bytes")
    return data


def load_raw_toc(iso: Path, toc_sector: int, sector_bytes: int):
    iso_size = iso.stat().st_size
    toc_offset = toc_sector * sector_bytes
    with iso.open("rb") as handle:
        header = read_exact(handle, toc_offset, 8)
        version = read_u32_bytes(header, 0)
        toc_size = read_u32_bytes(header, 4)
        if toc_size < LEVEL_TABLE_OFFSET + LEVEL_COUNT * 8:
            raise RuntimeError(
                f"TOC is too small for the R&C1 retail tail: 0x{toc_size:x}"
            )
        if toc_offset + toc_size > iso_size:
            raise RuntimeError("declared R&C1 TOC exceeds ISO size")
        toc = read_exact(handle, toc_offset, toc_size)

    raw_levels = []
    for index in range(LEVEL_COUNT):
        offset = LEVEL_TABLE_OFFSET + index * 8
        raw_levels.append(
            {
                "num": index,
                "start": read_u32_bytes(toc, offset),
                "length": read_u32_bytes(toc, offset + 4),
            }
        )
    return version, toc_size, toc, raw_levels


def parse_sector_range(header: bytes, offset: int) -> tuple[int, int]:
    return read_u32_bytes(header, offset), read_u32_bytes(header, offset + 4)


def inspect_level_header(handle, iso_size: int, sector_bytes: int, header_sector: int):
    if header_sector <= 0:
        return None
    header_offset = header_sector * sector_bytes
    if header_offset + AMALGAMATED_HEADER_BYTES > iso_size:
        return None

    handle.seek(header_offset)
    header = handle.read(AMALGAMATED_HEADER_BYTES)
    if len(header) != AMALGAMATED_HEADER_BYTES:
        return None
    if read_u32_bytes(header, 4) != AMALGAMATED_HEADER_BYTES:
        return None

    level_id = read_u32_bytes(header, 0)
    ranges = [
        parse_sector_range(header, 0x008),  # data
        parse_sector_range(header, 0x010),  # gameplay NTSC
        parse_sector_range(header, 0x018),  # gameplay PAL
        parse_sector_range(header, 0x020),  # occlusion
    ]

    # A real gameplay level must at least expose the level-data range. Rejecting
    # empty data also prevents accidental 0x2434 byte patterns elsewhere in the disc.
    if ranges[0][0] == 0 or ranges[0][1] == 0:
        return None

    header_sectors = (AMALGAMATED_HEADER_BYTES + sector_bytes - 1) // sector_bytes
    span_start = header_sector
    span_end = header_sector + header_sectors
    for start, count in ranges:
        if start == 0 or count == 0:
            continue
        end = start + count
        if end < start or end * sector_bytes > iso_size:
            return None
        span_start = min(span_start, start)
        span_end = max(span_end, end)

    if span_end <= span_start:
        return None
    span_bytes = (span_end - span_start) * sector_bytes
    if span_bytes > MAX_NATIVE_LEVEL_BYTES:
        return None

    return {
        "id": level_id,
        "header": header_sector,
        "start": span_start,
        "length": span_end - span_start,
    }


def discover_native_levels(iso: Path, toc: bytes, sector_bytes: int):
    """Discover authentic level headers from TOC sector references.

    Wrench's R&C1 reader deliberately scans every 8-byte TOC entry and checks
    whether the first sector points at an amalgamated header whose header_size is
    0x2434. We use the same format fact, then additionally validate the four
    renderer-relevant sector ranges and compute a safe contiguous extraction span.
    """
    iso_size = iso.stat().st_size
    discovered = []
    seen_headers: set[int] = set()

    with iso.open("rb") as handle:
        for toc_offset in range(8, len(toc) - 7, 8):
            header_sector = read_u32_bytes(toc, toc_offset)
            if header_sector in seen_headers:
                continue
            candidate = inspect_level_header(handle, iso_size, sector_bytes, header_sector)
            if candidate is None:
                continue
            seen_headers.add(header_sector)
            candidate["num"] = len(discovered)
            candidate["toc_offset"] = toc_offset
            discovered.append(candidate)

    return discovered


def update_toc_json(path: Path, version: int, toc_size: int, raw_levels, native_levels) -> None:
    toc = {}
    if path.exists():
        with path.open("r", encoding="utf-8") as handle:
            toc = json.load(handle)
        if int(toc.get("version", version)) != version:
            raise RuntimeError("build/toc.json version does not match the ISO")
        if int(toc.get("toc_size", toc_size)) != toc_size:
            raise RuntimeError("build/toc.json size does not match the ISO")

    toc["version"] = version
    toc["toc_size"] = toc_size
    # Preserve the exact 19 raw SectorRange entries for the game-visible TOC.
    toc["levels"] = raw_levels
    # Native spans are host extraction metadata. They are intentionally separate:
    # a raw TOC SectorRange is not assumed to describe a complete level file.
    toc["native_levels"] = native_levels
    toc.pop("leveldirs", None)

    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(toc, handle, indent=4)
        handle.write("\n")
    os.replace(temporary, path)


def extract_level(iso: Path, outdir: Path, entry, sector_bytes: int) -> Path:
    index = int(entry["num"])
    start = int(entry["start"])
    length = int(entry["length"])
    if start == 0 or length == 0:
        raise RuntimeError(f"native level {index} has no validated disc span")

    outdir.mkdir(parents=True, exist_ok=True)
    destination = outdir / f"level_{index:02d}.wad"
    bytes_left = length * sector_bytes

    with iso.open("rb") as source, destination.open("wb") as output:
        source.seek(start * sector_bytes)
        while bytes_left:
            chunk = source.read(min(bytes_left, CHUNK_BYTES))
            if not chunk:
                raise RuntimeError(f"unexpected end of ISO extracting native level {index}")
            output.write(chunk)
            bytes_left -= len(chunk)

    return destination


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("iso", type=Path)
    parser.add_argument("--toc", type=Path, required=True)
    parser.add_argument("--outdir", type=Path, required=True)
    parser.add_argument("--level", type=int, action="append", default=[])
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--toc-at", type=int, default=TOC_SECTOR)
    parser.add_argument("--blocksize", type=int, default=SECTOR_BYTES)
    args = parser.parse_args()

    iso = args.iso.resolve()
    if not iso.is_file():
        raise SystemExit(f"ISO not found: {iso}")
    if args.blocksize <= 0:
        raise SystemExit("--blocksize must be positive")

    version, toc_size, toc_bytes, raw_levels = load_raw_toc(
        iso, args.toc_at, args.blocksize
    )
    if args.toc_at == TOC_SECTOR and args.blocksize == SECTOR_BYTES and toc_size != RAC1_TOC_BYTES:
        print(
            f"warning: expected retail R&C1 TOC size 0x{RAC1_TOC_BYTES:x}, "
            f"found 0x{toc_size:x}",
            file=sys.stderr,
        )

    native_levels = discover_native_levels(iso, toc_bytes, args.blocksize)
    update_toc_json(args.toc.resolve(), version, toc_size, raw_levels, native_levels)
    print(
        f"[OpenRatchet:levels] toc version={version} size=0x{toc_size:x} "
        f"raw_entries={len(raw_levels)} discovered={len(native_levels)} "
        f"metadata={args.toc.resolve()}"
    )

    if not native_levels:
        raise SystemExit("no valid R&C1 amalgamated level headers found from TOC references")

    if args.all:
        selected = native_levels
    elif args.level:
        selected = []
        by_index = {int(entry["num"]): entry for entry in native_levels}
        for index in args.level:
            entry = by_index.get(index)
            if entry is None:
                raise SystemExit(f"native level index not discovered: {index}")
            selected.append(entry)
    else:
        selected = [native_levels[0]]

    for entry in selected:
        destination = extract_level(iso, args.outdir.resolve(), entry, args.blocksize)
        byte_count = int(entry["length"]) * args.blocksize
        print(
            f"[OpenRatchet:levels] extracted index={entry['num']} id={entry['id']} "
            f"header=0x{entry['header']:x} span=0x{entry['start']:x}+0x{entry['length']:x} "
            f"bytes=0x{byte_count:x} path={destination}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
