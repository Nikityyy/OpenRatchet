#!/usr/bin/env python3

import importlib.util
import json
import ntpath
import os
import struct
import sys
from pathlib import Path

AMALGAMATED_HEADER_BYTES = 0x2434
MAX_NATIVE_LEVEL_BYTES = 1024 * 1024 * 1024


def _read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _inspect_level_header(parser, header_sector: int):
    if header_sector <= 0:
        return None
    blocksize = parser.args.blocksize
    iso_size = os.fstat(parser.data.fileno()).st_size
    header_offset = header_sector * blocksize
    if header_offset + AMALGAMATED_HEADER_BYTES > iso_size:
        return None

    parser.data.seek(header_offset)
    header = parser.data.read(AMALGAMATED_HEADER_BYTES)
    if len(header) != AMALGAMATED_HEADER_BYTES:
        return None
    if _read_u32(header, 4) != AMALGAMATED_HEADER_BYTES:
        return None

    ranges = []
    for offset in (0x008, 0x010, 0x018, 0x020):
        ranges.append((_read_u32(header, offset), _read_u32(header, offset + 4)))
    if ranges[0][0] == 0 or ranges[0][1] == 0:
        return None

    header_sectors = (AMALGAMATED_HEADER_BYTES + blocksize - 1) // blocksize
    span_start = header_sector
    span_end = header_sector + header_sectors
    for start, count in ranges:
        if start == 0 or count == 0:
            continue
        end = start + count
        if end < start or end * blocksize > iso_size:
            return None
        span_start = min(span_start, start)
        span_end = max(span_end, end)
    if span_end <= span_start or (span_end - span_start) * blocksize > MAX_NATIVE_LEVEL_BYTES:
        return None

    return {
        "id": _read_u32(header, 0),
        "header": header_sector,
        "start": span_start,
        "length": span_end - span_start,
    }


def _discover_native_levels(parser):
    # Mirror the robust R&C1 discovery used by Wrench: every 8-byte TOC entry
    # may contain a sector reference, and an authentic level envelope is
    # identified by header_size == 0x2434 at that sector. This is deliberately
    # separate from the raw final 19 SectorRange values preserved in toc.json.
    parser.data.seek(parser.args.toc_at * parser.args.blocksize)
    header = parser.data.read(8)
    if len(header) != 8:
        return []
    toc_size = _read_u32(header, 4)
    parser.data.seek(parser.args.toc_at * parser.args.blocksize)
    toc = parser.data.read(toc_size)
    if len(toc) != toc_size:
        return []

    levels = []
    seen = set()
    for toc_offset in range(8, len(toc) - 7, 8):
        sector = _read_u32(toc, toc_offset)
        if sector in seen:
            continue
        candidate = _inspect_level_header(parser, sector)
        if candidate is None:
            continue
        seen.add(sector)
        candidate["num"] = len(levels)
        candidate["toc_offset"] = toc_offset
        levels.append(candidate)
    return levels


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit("usage: rac_toc_parser.py <upstream-tocparser.py> [arguments...]")

    parser_path = Path(sys.argv.pop(1)).resolve()
    spec = importlib.util.spec_from_file_location("rac_dvd_tocparser", parser_path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"could not load TOC parser: {parser_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    original_vag_header = module.TocParser.parse_vag_header
    original_parse_toc = module.TocParser.parse_toc
    original_dump_toc = module.TocParser.dump_toc
    original_copy_data = module.TocParser.copy_data

    def parse_vag_header(parser):
        header, length, filename = original_vag_header(parser)
        # Retail headers can retain developer paths such as Z:\I5\sound\spee.
        filename = ntpath.basename(filename.replace("/", "\\")) or "unnamed"
        return header, length, filename

    def parse_toc(parser):
        original_parse_toc(parser)
        # Preserve the exact retail tail as 19 raw SectorRange entries. These
        # bytes are required when OpenRatchet reconstructs the game-visible TOC,
        # but they are not assumed to be complete host-file extraction spans.
        if parser.args.leveldirs_count % 2 != 0:
            raise RuntimeError("R&C1 level table word count must be even")
        parser.levels = []
        for i in range(parser.args.leveldirs_count // 2):
            parser.levels.append(
                module.Sectlen(
                    num=i,
                    start=parser.read_int32(),
                    length=parser.read_int32(),
                )
            )
        parser.native_levels = _discover_native_levels(parser)

    def dump_toc(parser):
        original_dump_toc(parser)
        if not parser.args.dumptoc:
            return
        toc_path = Path(parser.args.dumptoc)
        with toc_path.open("r", encoding="utf-8") as handle:
            toc = json.load(handle)
        toc["levels"] = [entry._asdict() for entry in parser.levels]
        toc["native_levels"] = parser.native_levels
        toc.pop("leveldirs", None)
        with toc_path.open("w", encoding="utf-8", newline="\n") as handle:
            json.dump(toc, handle, indent=4)
            handle.write("\n")

    def copy_data(parser):
        original_copy_data(parser)
        level_dir = Path(parser.args.outdir).resolve() / "levels"
        level_dir.mkdir(parents=True, exist_ok=True)
        for entry in parser.native_levels:
            filepath = level_dir / f"level_{entry['num']:02d}.wad"
            parser.data.seek(entry["start"] * parser.args.blocksize)
            remaining = entry["length"] * parser.args.blocksize
            with filepath.open("wb") as output:
                while remaining:
                    chunk = parser.data.read(min(remaining, 8 * 1024 * 1024))
                    if not chunk:
                        raise RuntimeError(
                            f"unexpected end of disc extracting native level {entry['num']}"
                        )
                    output.write(chunk)
                    remaining -= len(chunk)
            print(filepath)

    module.TocParser.parse_vag_header = parse_vag_header
    module.TocParser.parse_toc = parse_toc
    module.TocParser.dump_toc = dump_toc
    module.TocParser.copy_data = copy_data
    module.main()


if __name__ == "__main__":
    main()
