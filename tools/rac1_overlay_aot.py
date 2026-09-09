#!/usr/bin/env python3
"""Build-time R&C1 level-overlay AOT preparation helpers.

This tool is intentionally offline/build-time only.  It extracts the executable
level code generation already present in the retail level WAD, wraps it in a
minimal MIPS ELF that PS2Recomp can consume, and post-processes a secondary
PS2Recomp output into a collision-free OpenRatchet AOT module.

It does not interpret MIPS at runtime, emulate PS2 hardware, synthesize gameplay
state, or depend on PCSX2.  PCSX2 may be supplied only as an optional byte oracle
for investigation-time validation.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

SECTOR_BYTES = 0x800
AMALGAMATED_HEADER_BYTES = 0x2434
LEVEL_DATA_HEADER_BYTES = 0x58
OVERLAY_RECORD_BYTES = 0x10
EE_RAM_BYTES = 0x02000000
ELF_HEADER_BYTES = 52
PROGRAM_HEADER_BYTES = 32
MIPS_ELF_FLAGS = 0x20924001
PF_X = 0x1
PF_W = 0x2
PF_R = 0x4
PT_LOAD = 1
ET_EXEC = 2
EM_MIPS = 8

REGISTER_ENTRY_RE = re.compile(
    r"g_ps2RecompiledFunctionTable\[(?P<slot>\d+)\]\s*=\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*;\s*//\s*0x(?P<pc>[0-9A-Fa-f]+)"
)
DECL_RE = re.compile(
    r"(?m)^void\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\(uint8_t\*\s*rdram,\s*R5900Context\*\s*ctx,\s*PS2Runtime\s*\*runtime\)\s*;"
)


GHIDRA_EXPORTER_PROTOCOL = "openratchet-entry0x-v1"


class OverlayAotError(RuntimeError):
    pass


@dataclass(frozen=True, slots=True)
class LevelLocation:
    level_id: int
    start_sector: int
    sector_count: int
    header_sector: int


@dataclass(frozen=True, slots=True)
class OverlaySegment:
    destination: int
    payload_size: int
    field8: int
    generation_entry: int
    payload_offset: int
    payload: bytes

    @property
    def end(self) -> int:
        return self.destination + self.payload_size


@dataclass(frozen=True, slots=True)
class OverlayImage:
    level_id: int
    generation_entry: int
    segments: tuple[OverlaySegment, ...]
    container_size: int

    @property
    def payload_bytes(self) -> int:
        return sum(segment.payload_size for segment in self.segments)

    @property
    def executable_segment(self) -> OverlaySegment:
        matches = [
            segment
            for segment in self.segments
            if segment.destination <= self.generation_entry < segment.end
        ]
        if len(matches) != 1:
            raise OverlayAotError(
                f"generation entry 0x{self.generation_entry:08x} is contained in "
                f"{len(matches)} overlay segments, expected exactly one"
            )
        return matches[0]


@dataclass(frozen=True, slots=True)
class GhidraFunctionRecord:
    name: str
    start: int
    end: int
    size: int


def _u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise OverlayAotError(f"u32 read 0x{offset:x} is outside {len(data)} bytes")
    return struct.unpack_from("<I", data, offset)[0]


def load_level_location(toc_path: Path, level_id: int) -> LevelLocation:
    try:
        toc = json.loads(toc_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise OverlayAotError(f"cannot read TOC {toc_path}: {exc}") from exc

    candidates = [item for item in toc.get("native_levels", []) if int(item.get("id", -1)) == level_id]
    if len(candidates) != 1:
        raise OverlayAotError(
            f"TOC {toc_path} contains {len(candidates)} native_levels entries for level {level_id}, expected 1"
        )
    item = candidates[0]
    return LevelLocation(
        level_id=level_id,
        start_sector=int(item["start"]),
        sector_count=int(item["length"]),
        header_sector=int(item["header"]),
    )


def parse_level_overlay(wad: bytes, location: LevelLocation) -> OverlayImage:
    expected_bytes = location.sector_count * SECTOR_BYTES
    if len(wad) != expected_bytes:
        raise OverlayAotError(
            f"level WAD size mismatch: got {len(wad)} bytes, TOC requires {expected_bytes}"
        )
    if location.header_sector < location.start_sector:
        raise OverlayAotError("level header sector precedes level start")

    header_offset = (location.header_sector - location.start_sector) * SECTOR_BYTES
    if header_offset + AMALGAMATED_HEADER_BYTES > len(wad):
        raise OverlayAotError("amalgamated level header is truncated")

    level_id = _u32(wad, header_offset + 0x00)
    header_size = _u32(wad, header_offset + 0x04)
    if level_id != location.level_id:
        raise OverlayAotError(f"level id mismatch: WAD={level_id} TOC={location.level_id}")
    if header_size != AMALGAMATED_HEADER_BYTES:
        raise OverlayAotError(
            f"unexpected amalgamated header size 0x{header_size:x}, expected 0x{AMALGAMATED_HEADER_BYTES:x}"
        )

    data_start_sector = _u32(wad, header_offset + 0x08)
    data_sector_count = _u32(wad, header_offset + 0x0C)
    if data_start_sector < location.start_sector or data_sector_count == 0:
        raise OverlayAotError("invalid level-data sector range")
    data_offset = (data_start_sector - location.start_sector) * SECTOR_BYTES
    data_bytes = data_sector_count * SECTOR_BYTES
    if data_offset + data_bytes > len(wad) or data_bytes < LEVEL_DATA_HEADER_BYTES:
        raise OverlayAotError("level-data sector range lies outside the extracted level WAD")

    overlay_offset = _u32(wad, data_offset + 0x00)
    overlay_size = _u32(wad, data_offset + 0x04)
    if overlay_size == 0:
        raise OverlayAotError("level has no executable overlay container")
    if overlay_offset > data_bytes or overlay_size > data_bytes - overlay_offset:
        raise OverlayAotError("overlay byte range lies outside level-data range")

    container = wad[data_offset + overlay_offset:data_offset + overlay_offset + overlay_size]
    cursor = 0
    segments: list[OverlaySegment] = []
    generation_entry: int | None = None
    while cursor < len(container):
        if len(container) - cursor < OVERLAY_RECORD_BYTES:
            raise OverlayAotError(f"truncated overlay record header at 0x{cursor:x}")
        destination, payload_size, field8, field_c = struct.unpack_from("<IIII", container, cursor)
        payload_offset = cursor + OVERLAY_RECORD_BYTES
        if payload_size > len(container) - payload_offset:
            raise OverlayAotError(f"overlay payload at 0x{cursor:x} is truncated")
        if destination > EE_RAM_BYTES or payload_size > EE_RAM_BYTES - destination:
            raise OverlayAotError(
                f"overlay destination 0x{destination:08x}+0x{payload_size:x} lies outside EE RAM"
            )
        if field_c == 0:
            raise OverlayAotError(
                f"unexpected zero generation entry inside declared overlay container at record 0x{cursor:x}"
            )
        if generation_entry is None:
            generation_entry = field_c
        elif field_c != generation_entry:
            raise OverlayAotError(
                f"overlay records disagree on generation entry: 0x{generation_entry:08x} vs 0x{field_c:08x}"
            )

        payload = container[payload_offset:payload_offset + payload_size]
        segments.append(
            OverlaySegment(
                destination=destination,
                payload_size=payload_size,
                field8=field8,
                generation_entry=field_c,
                payload_offset=payload_offset,
                payload=payload,
            )
        )
        cursor = payload_offset + payload_size

    if cursor != len(container):
        raise OverlayAotError(f"overlay parser ended at {cursor}, container has {len(container)} bytes")
    if not segments or generation_entry is None:
        raise OverlayAotError("overlay container has no segments")

    image = OverlayImage(
        level_id=level_id,
        generation_entry=generation_entry,
        segments=tuple(segments),
        container_size=len(container),
    )
    _ = image.executable_segment  # hard invariant: entry belongs to exactly one segment
    return image


def parse_raw_overlay_container(data: bytes, image_id: int) -> OverlayImage:
    """Parse a sector-padded Retail overlay WAD whose records start at byte 0.

    Some early R&C1 generations (notably ``wads/wad_158.wad``) are already the
    raw record stream consumed by ``sub_0012D8F8`` rather than a LevelDataHeader
    embedded inside an amalgamated level WAD.  Retail terminates the record group
    with zero-filled sector padding.  We accept *only* all-zero trailing bytes so
    corruption cannot be mistaken for padding.
    """
    if not data:
        raise OverlayAotError("raw overlay container is empty")

    cursor = 0
    segments: list[OverlaySegment] = []
    generation_entry: int | None = None
    while cursor + OVERLAY_RECORD_BYTES <= len(data):
        destination, payload_size, field8, field_c = struct.unpack_from("<IIII", data, cursor)
        if destination == 0 and payload_size == 0 and field8 == 0 and field_c == 0:
            break
        payload_offset = cursor + OVERLAY_RECORD_BYTES
        if payload_size == 0:
            raise OverlayAotError(f"raw overlay record at 0x{cursor:x} has zero payload size")
        if payload_size > len(data) - payload_offset:
            raise OverlayAotError(f"raw overlay payload at 0x{cursor:x} is truncated")
        if destination > EE_RAM_BYTES or payload_size > EE_RAM_BYTES - destination:
            raise OverlayAotError(
                f"raw overlay destination 0x{destination:08x}+0x{payload_size:x} lies outside EE RAM"
            )
        if field_c == 0:
            raise OverlayAotError(f"raw overlay record at 0x{cursor:x} has zero generation entry")
        if generation_entry is None:
            generation_entry = field_c
        elif field_c != generation_entry:
            raise OverlayAotError(
                f"raw overlay records disagree on generation entry: "
                f"0x{generation_entry:08x} vs 0x{field_c:08x}"
            )
        payload = data[payload_offset:payload_offset + payload_size]
        segments.append(
            OverlaySegment(
                destination=destination,
                payload_size=payload_size,
                field8=field8,
                generation_entry=field_c,
                payload_offset=payload_offset,
                payload=payload,
            )
        )
        cursor = payload_offset + payload_size

    if not segments or generation_entry is None:
        raise OverlayAotError("raw overlay container has no records")
    if any(data[cursor:]):
        first = next(i for i, value in enumerate(data[cursor:], start=cursor) if value)
        raise OverlayAotError(f"raw overlay has non-zero trailing data at 0x{first:x}")

    image = OverlayImage(
        level_id=image_id,
        generation_entry=generation_entry,
        segments=tuple(segments),
        container_size=cursor,
    )
    _ = image.executable_segment
    return image


def direct_overlay_targets(image: OverlayImage) -> dict[str, object]:
    """Return direct J/JAL structure for the executable Retail generation.

    Function discovery remains Ghidra/PS2Recomp-owned.  These sets are independent
    structural evidence used to reject a Ghidra export that somehow missed a direct
    callable target from the exact WAD bytes.
    """
    executable = image.executable_segment
    ranges = [(segment.destination, segment.end) for segment in image.segments]
    inside_j: set[int] = set()
    inside_jal: set[int] = set()
    external: set[int] = set()
    j_instructions = 0
    jal_instructions = 0
    external_instructions = 0
    for offset in range(0, len(executable.payload) - 3, 4):
        raw = struct.unpack_from("<I", executable.payload, offset)[0]
        opcode = raw >> 26
        if opcode not in (0x02, 0x03):
            continue
        pc = executable.destination + offset
        target = ((pc + 4) & 0xF0000000) | ((raw & 0x03FFFFFF) << 2)
        inside = any(start <= target < end for start, end in ranges)
        if inside:
            if opcode == 0x02:
                j_instructions += 1
                inside_j.add(target)
            else:
                jal_instructions += 1
                inside_jal.add(target)
        else:
            external_instructions += 1
            external.add(target)
    return {
        "j_inside": j_instructions,
        "jal_inside": jal_instructions,
        "direct_external": external_instructions,
        "j_targets": inside_j,
        "jal_targets": inside_jal,
        "external_targets": external,
    }


def count_direct_overlay_targets(image: OverlayImage) -> dict[str, int]:
    result = direct_overlay_targets(image)
    return {
        "j_inside": int(result["j_inside"]),
        "jal_inside": int(result["jal_inside"]),
        "direct_external": int(result["direct_external"]),
    }


def level0_registry_callback_targets(image: OverlayImage) -> set[int]:
    """Return consumer-proved Level-0 registry callback entry PCs.

    The relocated Level-0 producer consumes the exact 0x1EA300 table.  Its overlay
    record is 0x504 bytes = 107 fixed 12-byte records; word +0x04 is the callback
    slot.  Zero is the explicit absent-callback value.  This is a stronger source
    of indirect entry points than guessing from pointer-looking data.
    """
    if image.level_id != 0:
        return set()
    matches = [segment for segment in image.segments if segment.destination == 0x001EA300]
    if len(matches) != 1:
        raise OverlayAotError(f"Level 0 must contain exactly one 0x1EA300 registry segment, got {len(matches)}")
    segment = matches[0]
    if segment.payload_size != 0x504 or segment.payload_size % 12 != 0:
        raise OverlayAotError(
            f"Level-0 registry segment has unexpected size 0x{segment.payload_size:x}"
        )
    executable = image.executable_segment
    targets: set[int] = set()
    for offset in range(0, segment.payload_size, 12):
        callback = _u32(segment.payload, offset + 4)
        if callback == 0:
            continue
        if (callback & 3) != 0 or not (executable.destination <= callback < executable.end):
            raise OverlayAotError(
                f"Level-0 registry callback at 0x{segment.destination + offset + 4:08x} "
                f"is not executable overlay code: 0x{callback:08x}"
            )
        targets.add(callback)
    if len(targets) != 75:
        raise OverlayAotError(
            f"Level-0 registry has {len(targets)} unique non-null callbacks, expected 75"
        )
    return targets


def augment_ghidra_map_with_required_entries(
    image: OverlayImage,
    source_csv: Path,
    output_csv: Path,
) -> dict[str, object]:
    """Add Retail-proved callable entries without guessing function boundaries.

    Ghidra remains authoritative for function *bodies*.  The exact Retail WAD is
    independently authoritative for direct JAL destinations, and the Level-0
    registry is consumer-proof for additional indirect callback entries.  Ghidra
    can legitimately absorb one of those callable PCs into a larger function body
    instead of creating a distinct Function object.  In that case we add an
    alternate entry that reuses the containing Ghidra-proved end.

    If a WAD-proved callable target is not contained in any analyzed Ghidra body,
    generation still fails closed; no function boundary is invented.
    """
    records = list(read_ghidra_csv(source_csv))
    records.sort(key=lambda record: (record.start, record.end, record.name))
    existing = {record.start for record in records}
    direct_jal = set(direct_overlay_targets(image)["jal_targets"])
    registry = level0_registry_callback_targets(image)
    required = direct_jal | registry
    added: list[GhidraFunctionRecord] = []
    added_direct = 0
    added_registry = 0
    for target in sorted(required - existing):
        owners = [record for record in records if record.start < target < record.end]
        if not owners:
            kinds = []
            if target in direct_jal:
                kinds.append("direct JAL target")
            if target in registry:
                kinds.append("registry callback")
            kind = "/".join(kinds) or "Retail callable target"
            raise OverlayAotError(
                f"Retail {kind} 0x{target:08x} is not contained in any Ghidra function; "
                "refusing to guess its function boundary"
            )
        # Prefer the tightest containing Ghidra body if analysis produced nested ranges.
        owner = min(owners, key=lambda record: (record.end - record.start, -record.start))
        added.append(
            GhidraFunctionRecord(
                name=f"or_entry_{target:08x}",
                start=target,
                end=owner.end,
                size=owner.end - target,
            )
        )
        if target in direct_jal:
            added_direct += 1
        if target in registry:
            added_registry += 1
    combined = sorted(records + added, key=lambda record: (record.start, record.end, record.name))
    output_csv.parent.mkdir(parents=True, exist_ok=True)
    with output_csv.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["Name", "Start", "End", "Size"])
        for record in combined:
            writer.writerow(
                [record.name, f"0x{record.start:08X}", f"0x{record.end:08X}", record.size]
            )
    return {
        "source_records": len(records),
        "required_direct_jal_entries": len(direct_jal),
        "required_registry_entries": len(registry),
        "required_callable_entries": len(required),
        "synthetic_direct_jal_entries": added_direct,
        "synthetic_registry_entries": added_registry,
        "synthetic_alternate_entries": len(added),
        "output_records": len(combined),
    }


def validate_oracle_ram(image: OverlayImage, ram: bytes) -> dict[str, object]:
    if len(ram) != EE_RAM_BYTES:
        raise OverlayAotError(f"oracle EE RAM is {len(ram)} bytes, expected {EE_RAM_BYTES}")
    segments: list[dict[str, object]] = []
    executable_match = False
    for index, segment in enumerate(image.segments):
        live = ram[segment.destination:segment.end]
        match = live == segment.payload
        if segment is image.executable_segment:
            executable_match = match
        segments.append(
            {
                "index": index,
                "destination": f"0x{segment.destination:08x}",
                "bytes": segment.payload_size,
                "match": match,
                "wad_sha256": hashlib.sha256(segment.payload).hexdigest(),
                "ram_sha256": hashlib.sha256(live).hexdigest(),
            }
        )
    if not executable_match:
        executable = image.executable_segment
        raise OverlayAotError(
            f"Retail oracle does not match the WAD executable overlay at "
            f"0x{executable.destination:08x}..0x{executable.end:08x}"
        )
    return {"executable_match": executable_match, "segments": segments}


def _align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def build_overlay_elf(image: OverlayImage) -> bytes:
    """Build a deterministic minimal ELF32/MIPS image for the Retail overlay generation."""
    phnum = len(image.segments)
    payload_cursor = _align(ELF_HEADER_BYTES + phnum * PROGRAM_HEADER_BYTES, 0x1000)
    file_offsets: list[int] = []
    for segment in image.segments:
        file_offsets.append(payload_cursor)
        payload_cursor = _align(payload_cursor + segment.payload_size, 0x10)

    out = bytearray(payload_cursor)
    ident = bytearray(16)
    ident[0:4] = b"\x7fELF"
    ident[4] = 1  # ELFCLASS32
    ident[5] = 1  # ELFDATA2LSB
    ident[6] = 1  # EV_CURRENT
    out[0:16] = ident
    struct.pack_into(
        "<HHIIIIIHHHHHH",
        out,
        16,
        ET_EXEC,
        EM_MIPS,
        1,
        image.generation_entry,
        ELF_HEADER_BYTES,
        0,  # no section table; PS2Recomp falls back to PT_LOAD segments
        MIPS_ELF_FLAGS,
        ELF_HEADER_BYTES,
        PROGRAM_HEADER_BYTES,
        phnum,
        0,
        0,
        0,
    )

    executable = image.executable_segment
    for index, (segment, file_offset) in enumerate(zip(image.segments, file_offsets, strict=True)):
        flags = PF_R | (PF_X if segment is executable else PF_W)
        struct.pack_into(
            "<IIIIIIII",
            out,
            ELF_HEADER_BYTES + index * PROGRAM_HEADER_BYTES,
            PT_LOAD,
            file_offset,
            segment.destination,
            segment.destination,
            segment.payload_size,
            segment.payload_size,
            flags,
            0x10,
        )
        out[file_offset:file_offset + segment.payload_size] = segment.payload

    return bytes(out)


def parse_elf_program_headers(elf: bytes) -> dict[str, object]:
    if len(elf) < ELF_HEADER_BYTES or elf[:4] != b"\x7fELF":
        raise OverlayAotError("not an ELF image")
    if elf[4:7] != b"\x01\x01\x01":
        raise OverlayAotError("expected ELF32 little-endian current-version image")
    e_type, e_machine, _version, entry, phoff, shoff, flags = struct.unpack_from("<HHIIIII", elf, 16)
    ehsize, phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from("<HHHHHH", elf, 40)
    if e_type != ET_EXEC or e_machine != EM_MIPS:
        raise OverlayAotError("synthetic ELF is not MIPS ET_EXEC")
    headers = []
    for index in range(phnum):
        off = phoff + index * phentsize
        if off + PROGRAM_HEADER_BYTES > len(elf):
            raise OverlayAotError("program header table is truncated")
        p_type, p_offset, vaddr, paddr, filesz, memsz, p_flags, align = struct.unpack_from(
            "<IIIIIIII", elf, off
        )
        if p_offset + filesz > len(elf):
            raise OverlayAotError("program payload lies outside ELF")
        headers.append(
            {
                "type": p_type,
                "offset": p_offset,
                "vaddr": vaddr,
                "paddr": paddr,
                "filesz": filesz,
                "memsz": memsz,
                "flags": p_flags,
                "align": align,
            }
        )
    return {
        "entry": entry,
        "flags": flags,
        "ehsize": ehsize,
        "phentsize": phentsize,
        "phnum": phnum,
        "shoff": shoff,
        "shentsize": shentsize,
        "shnum": shnum,
        "shstrndx": shstrndx,
        "program_headers": headers,
    }


def overlay_manifest(image: OverlayImage, elf: bytes) -> dict[str, object]:
    executable = image.executable_segment
    direct_targets = count_direct_overlay_targets(image)
    return {
        "schema": 1,
        "kind": "RAC1_LEVEL_OVERLAY_AOT_INPUT",
        "level_id": image.level_id,
        "generation_entry": f"0x{image.generation_entry:08x}",
        "segment_count": len(image.segments),
        "payload_bytes": image.payload_bytes,
        "container_bytes": image.container_size,
        "executable": {
            "start": f"0x{executable.destination:08x}",
            "end": f"0x{executable.end:08x}",
            "bytes": executable.payload_size,
            "sha256": hashlib.sha256(executable.payload).hexdigest(),
        },
        "direct_targets": direct_targets,
        "elf_sha256": hashlib.sha256(elf).hexdigest(),
        "segments": [
            {
                "destination": f"0x{segment.destination:08x}",
                "end": f"0x{segment.end:08x}",
                "payload_bytes": segment.payload_size,
                "field8": segment.field8,
                "generation_entry": f"0x{segment.generation_entry:08x}",
                "sha256": hashlib.sha256(segment.payload).hexdigest(),
                "executable": segment is executable,
            }
            for segment in image.segments
        ],
    }


def write_ps2recomp_config(
    path: Path,
    elf_path: Path,
    output_path: Path,
    ghidra_csv: Path | None = None,
) -> None:
    ghidra_value = ghidra_csv.resolve().as_posix() if ghidra_csv is not None else ""
    content = (
        "[general]\n"
        f'input = "{elf_path.resolve().as_posix()}"\n'
        f'output = "{output_path.resolve().as_posix()}"\n'
        f'ghidra_output = "{ghidra_value}"\n'
        "single_file_output = false\n"
        "low_memory_mode = true\n"
        "output_worker_threads = 0\n"
        "patch_syscalls = false\n"
        "patch_cop0 = true\n"
        "patch_cache = true\n"
        "stubs = []\n"
        "skip = []\n"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def read_ghidra_csv(path: Path) -> tuple[GhidraFunctionRecord, ...]:
    try:
        with path.open("r", encoding="utf-8-sig", newline="") as stream:
            rows = csv.DictReader(stream)
            if rows.fieldnames != ["Name", "Start", "End", "Size"]:
                raise OverlayAotError(
                    f"unexpected Ghidra CSV header in {path}: {rows.fieldnames!r}"
                )
            records: list[GhidraFunctionRecord] = []
            for line, row in enumerate(rows, start=2):
                try:
                    name = (row["Name"] or "").strip()
                    start = int((row["Start"] or "").strip(), 0)
                    end = int((row["End"] or "").strip(), 0)
                    size = int((row["Size"] or "").strip(), 0)
                except (KeyError, TypeError, ValueError) as exc:
                    raise OverlayAotError(f"invalid Ghidra CSV record at {path}:{line}") from exc
                if not name:
                    raise OverlayAotError(f"empty Ghidra function name at {path}:{line}")
                records.append(GhidraFunctionRecord(name, start, end, size))
    except OSError as exc:
        raise OverlayAotError(f"cannot read Ghidra CSV {path}: {exc}") from exc
    if not records:
        raise OverlayAotError(f"Ghidra CSV {path} contains no functions")
    return tuple(records)


def validate_ghidra_map(
    image: OverlayImage,
    csv_path: Path,
    *,
    require_direct_jal_targets: bool = True,
) -> dict[str, object]:
    executable = image.executable_segment
    records = read_ghidra_csv(csv_path)
    starts: set[int] = set()
    for record in records:
        if record.start in starts:
            raise OverlayAotError(f"duplicate Ghidra function start 0x{record.start:08x}")
        starts.add(record.start)
        if (record.start & 3) != 0 or (record.end & 3) != 0:
            raise OverlayAotError(f"unaligned Ghidra function range {record.name}")
        if not (executable.destination <= record.start < record.end <= executable.end):
            raise OverlayAotError(
                f"Ghidra function {record.name} 0x{record.start:08x}..0x{record.end:08x} "
                f"lies outside executable overlay 0x{executable.destination:08x}..0x{executable.end:08x}"
            )
        if record.size <= 0 or record.size > record.end - record.start:
            # Ghidra Size is body-address count and may be smaller than the max-address
            # span for a non-contiguous function, but it may never be zero or larger.
            raise OverlayAotError(f"invalid Ghidra body size for {record.name}")

    if image.generation_entry not in starts:
        raise OverlayAotError(
            f"Ghidra map does not contain Retail generation entry 0x{image.generation_entry:08x}"
        )

    direct = direct_overlay_targets(image)
    missing_jal = sorted(set(direct["jal_targets"]) - starts)
    if require_direct_jal_targets and missing_jal:
        sample = ", ".join(f"0x{pc:08x}" for pc in missing_jal[:8])
        raise OverlayAotError(
            f"Ghidra map missed {len(missing_jal)} direct in-overlay JAL target(s): {sample}"
        )

    return {
        "records": len(records),
        "first_pc": min(starts),
        "last_pc": max(starts),
        "generation_entry": image.generation_entry,
        "unique_direct_jal_targets": len(set(direct["jal_targets"])),
        "missing_direct_jal_targets": len(missing_jal),
    }


def prepare_overlay_aot(
    toc_path: Path,
    wad_path: Path,
    level_id: int,
    work_dir: Path,
    oracle_ram: Path | None = None,
) -> dict[str, object]:
    location = load_level_location(toc_path, level_id)
    try:
        wad = wad_path.read_bytes()
    except OSError as exc:
        raise OverlayAotError(f"cannot read level WAD {wad_path}: {exc}") from exc
    image = parse_level_overlay(wad, location)
    elf = build_overlay_elf(image)
    parsed = parse_elf_program_headers(elf)

    work_dir.mkdir(parents=True, exist_ok=True)
    elf_path = work_dir / f"level_{level_id:02d}_overlay.elf"
    raw_output = work_dir / "raw"
    config_path = work_dir / "ps2recomp.toml"
    manifest_path = work_dir / "manifest.json"
    if raw_output.exists():
        shutil.rmtree(raw_output)
    raw_output.mkdir(parents=True)
    elf_path.write_bytes(elf)
    write_ps2recomp_config(config_path, elf_path, raw_output)

    manifest = overlay_manifest(image, elf)
    manifest["elf"] = {
        "entry": f"0x{parsed['entry']:08x}",
        "program_headers": parsed["program_headers"],
    }
    if oracle_ram is not None:
        try:
            ram = oracle_ram.read_bytes()
        except OSError as exc:
            raise OverlayAotError(f"cannot read oracle RAM {oracle_ram}: {exc}") from exc
        manifest["oracle"] = validate_oracle_ram(image, ram)
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def _symbol_mapping(function_header: str, prefix: str) -> dict[str, str]:
    names = sorted(set(match.group("name") for match in DECL_RE.finditer(function_header)))
    if not names:
        raise OverlayAotError("PS2Recomp output contains no function declarations")
    return {name: f"{prefix}{name}" for name in names}


def _compile_symbol_rewriter(mapping: dict[str, str]) -> re.Pattern[str]:
    if not mapping:
        raise OverlayAotError("cannot compile an empty symbol-rewrite map")
    # One alternation is dramatically faster than running one regex over every
    # generated translation unit for every symbol (803 x ~803 for wad_158).
    # Longest-first keeps deterministic behavior for suffix-related identifiers.
    names = sorted(mapping, key=lambda value: (-len(value), value))
    return re.compile(r"\b(?:" + "|".join(re.escape(name) for name in names) + r")\b")


def _replace_symbols(
    text: str,
    mapping: dict[str, str],
    pattern: re.Pattern[str] | None = None,
) -> str:
    pattern = pattern or _compile_symbol_rewriter(mapping)
    return pattern.sub(lambda match: mapping[match.group(0)], text)


def _parse_registration(register_text: str) -> list[tuple[int, str]]:
    entries: list[tuple[int, str]] = []
    seen: set[int] = set()
    for match in REGISTER_ENTRY_RE.finditer(register_text):
        pc = int(match.group("pc"), 16)
        name = match.group("name")
        if pc in seen:
            raise OverlayAotError(f"duplicate generated registration PC 0x{pc:08x}")
        seen.add(pc)
        entries.append((pc, name))
    if not entries:
        raise OverlayAotError("PS2Recomp register_functions.cpp contains no generated entries")
    return entries


def verify_ps2recomp_output(
    image: OverlayImage,
    ghidra_csv: Path,
    raw_dir: Path,
) -> dict[str, object]:
    ghidra = validate_ghidra_map(image, ghidra_csv)
    register_path = raw_dir / "register_functions.cpp"
    if not register_path.is_file():
        raise OverlayAotError(f"missing PS2Recomp registration output: {register_path}")
    registrations = _parse_registration(register_path.read_text(encoding="utf-8"))
    registered = {pc for pc, _ in registrations}
    starts = {record.start for record in read_ghidra_csv(ghidra_csv)}
    missing = sorted(starts - registered)
    if missing:
        sample = ", ".join(f"0x{pc:08x}" for pc in missing[:8])
        raise OverlayAotError(
            f"PS2Recomp omitted {len(missing)} Ghidra entry point(s) from dispatch registration: {sample}"
        )
    executable = image.executable_segment
    outside = sorted(pc for pc in registered if not (executable.destination <= pc < executable.end))
    if outside:
        sample = ", ".join(f"0x{pc:08x}" for pc in outside[:8])
        raise OverlayAotError(
            f"PS2Recomp overlay output registered {len(outside)} PC(s) outside the executable generation: {sample}"
        )
    if image.generation_entry not in registered:
        raise OverlayAotError(
            f"PS2Recomp output does not register generation entry 0x{image.generation_entry:08x}"
        )
    registry_targets = level0_registry_callback_targets(image)
    missing_registry = sorted(registry_targets - registered)
    if missing_registry:
        sample = ", ".join(f"0x{pc:08x}" for pc in missing_registry[:8])
        raise OverlayAotError(
            f"PS2Recomp omitted {len(missing_registry)} consumer-proved Retail registry callback(s): {sample}"
        )
    return {
        "ghidra_records": int(ghidra["records"]),
        "registered_entries": len(registered),
        "resume_entries": len(registered - starts),
        "registry_callback_entries": len(registry_targets),
        "missing_ghidra_entries": 0,
        "missing_registry_entries": 0,
        "first_pc": min(registered),
        "last_pc": max(registered),
    }


def _validate_materialized_ranges(
    materialized_ranges: Sequence[tuple[int, int]],
) -> tuple[tuple[int, int], ...]:
    ranges = tuple((int(begin), int(end)) for begin, end in materialized_ranges)
    if not ranges:
        raise OverlayAotError("overlay module requires at least one materialized range")
    for index, (begin, end) in enumerate(ranges):
        if begin < 0 or end > EE_RAM_BYTES or begin >= end or (begin & 3) != 0 or (end & 3) != 0:
            raise OverlayAotError(
                f"invalid materialized range {index}: 0x{begin:08x}:0x{end:08x}"
            )
        for prior_index in range(index):
            prior_begin, prior_end = ranges[prior_index]
            if begin < prior_end and prior_begin < end:
                raise OverlayAotError(
                    "overlapping materialized ranges: "
                    f"0x{prior_begin:08x}:0x{prior_end:08x} and "
                    f"0x{begin:08x}:0x{end:08x}"
                )
    return ranges


def _materialized_range_header_block() -> str:
    return (
        "// BEGIN OpenRatchet materialized ranges\n"
        "using MaterializedRange = ratchet::game::Rac1OverlayMaterializedRange;\n"
        "extern const MaterializedRange kMaterializedRanges[];\n"
        "extern const std::size_t kMaterializedRangeCount;\n"
        "// END OpenRatchet materialized ranges\n"
    )


def _materialized_range_cpp_block(
    materialized_ranges: Sequence[tuple[int, int]],
) -> str:
    lines = [
        "// BEGIN OpenRatchet materialized ranges",
        "const MaterializedRange kMaterializedRanges[] = {",
    ]
    for begin, end in materialized_ranges:
        lines.append(f"    {{0x{begin:08x}u, 0x{end:08x}u}},")
    lines.extend(
        [
            "};",
            "const std::size_t kMaterializedRangeCount =",
            "    sizeof(kMaterializedRanges) / sizeof(kMaterializedRanges[0]);",
            "// END OpenRatchet materialized ranges",
        ]
    )
    return "\n".join(lines) + "\n"


V7_METADATA_UPGRADE_SOURCE_TOOL_SHA256 = "48234e045b24204ef36277c196d1f0eca83f63da184d6ef134fc46e198bfdf09"


def _v7_metadata_upgrade_compatible(
    prior_inputs: dict[str, object],
    current_inputs: dict[str, object],
) -> bool:
    if prior_inputs.get("schema") != "rac1-overlay-aot-v7":
        return False
    if current_inputs.get("schema") != "rac1-overlay-aot-v8":
        return False
    # This metadata-only migration is intentionally pinned to the exact v7
    # generator shipped immediately before range ownership was added. If a user
    # has any other v7 tool, fail closed and perform the normal full regeneration.
    if prior_inputs.get("tool") != V7_METADATA_UPGRADE_SOURCE_TOOL_SHA256:
        return False
    ignored = {"tool", "schema"}
    prior_stable = {key: value for key, value in prior_inputs.items() if key not in ignored}
    current_stable = {key: value for key, value in current_inputs.items() if key not in ignored}
    return prior_stable == current_stable


def _upgrade_overlay_materialized_range_metadata(
    output_dir: Path,
    namespace_name: str,
    materialized_ranges: Sequence[tuple[int, int]],
) -> None:
    """Upgrade only generated dispatch metadata without regenerating AOT bodies.

    This is intentionally narrow: v7 already proved and cached every function body.
    v8 adds the exact Retail record ownership intervals needed by the runtime dispatch
    layer.  Rewriting only the two tiny table files avoids invalidating thousands of
    byte-identical generated C++ bodies and therefore avoids a full Ghidra/PS2Recomp
    and MSVC rebuild on the migration run.
    """
    ranges = _validate_materialized_ranges(materialized_ranges)
    header_path = output_dir / "openratchet_overlay_table.h"
    cpp_path = output_dir / "openratchet_overlay_table.cpp"
    if not header_path.is_file() or not cpp_path.is_file():
        raise OverlayAotError(
            f"cached overlay module is missing {header_path.name} or {cpp_path.name}"
        )

    header = header_path.read_text(encoding="utf-8")
    header_begin = "// BEGIN OpenRatchet materialized ranges\n"
    header_end = "// END OpenRatchet materialized ranges\n"
    header_block = _materialized_range_header_block()
    if header_begin in header:
        begin = header.index(header_begin)
        end = header.index(header_end, begin) + len(header_end)
        header = header[:begin] + header_block + header[end:]
    else:
        needle = "using FunctionEntry = ratchet::game::Rac1OverlayFunctionEntry;\n"
        if needle not in header:
            raise OverlayAotError("cached overlay table header has unexpected format")
        header = header.replace(needle, needle + header_block, 1)
    header_path.write_text(header, encoding="utf-8")

    cpp = cpp_path.read_text(encoding="utf-8")
    cpp_begin = "// BEGIN OpenRatchet materialized ranges\n"
    cpp_end = "// END OpenRatchet materialized ranges\n"
    cpp_block = _materialized_range_cpp_block(ranges)
    if cpp_begin in cpp:
        begin = cpp.index(cpp_begin)
        end = cpp.index(cpp_end, begin) + len(cpp_end)
        cpp = cpp[:begin] + cpp_block + cpp[end:]
    else:
        needle = f"namespace {namespace_name} {{\n"
        if needle not in cpp:
            raise OverlayAotError("cached overlay table source has unexpected namespace")
        cpp = cpp.replace(needle, needle + cpp_block, 1)
    cpp_path.write_text(cpp, encoding="utf-8")


def finalize_overlay_module(
    raw_dir: Path,
    output_dir: Path,
    prefix: str,
    namespace_name: str,
    generation_entry: int,
    materialized_ranges: Sequence[tuple[int, int]],
) -> dict[str, object]:
    header_path = raw_dir / "ps2_recompiled_functions.h"
    register_path = raw_dir / "register_functions.cpp"
    if not header_path.is_file() or not register_path.is_file():
        raise OverlayAotError(
            f"raw PS2Recomp output must contain {header_path.name} and {register_path.name}"
        )
    ranges = _validate_materialized_ranges(materialized_ranges)
    if not any(begin <= generation_entry < end for begin, end in ranges):
        raise OverlayAotError(
            f"generation entry 0x{generation_entry:08x} lies outside all materialized ranges"
        )

    function_header = header_path.read_text(encoding="utf-8")
    register_text = register_path.read_text(encoding="utf-8")
    mapping = _symbol_mapping(function_header, prefix)
    registrations = _parse_registration(register_text)
    outside = [
        pc for pc, _ in registrations
        if not any(begin <= pc < end for begin, end in ranges)
    ]
    if outside:
        raise OverlayAotError(
            f"overlay table contains {len(outside)} callable entry(s) outside materialized ranges; "
            f"first=0x{outside[0]:08x}"
        )
    if generation_entry not in {pc for pc, _ in registrations}:
        raise OverlayAotError(
            f"generation entry 0x{generation_entry:08x} is absent from finalized registrations"
        )
    unknown = sorted({name for _, name in registrations if name not in mapping})
    if unknown:
        raise OverlayAotError(
            "registration references symbols absent from ps2_recompiled_functions.h: "
            + ", ".join(unknown[:8])
        )

    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True)

    sources = [
        source
        for source in sorted(raw_dir.iterdir())
        if source.is_file() and source.name != "register_functions.cpp"
    ]
    symbol_pattern = _compile_symbol_rewriter(mapping)
    print(
        f"[overlay-aot] Finalizing {len(sources)} generated file(s) with "
        f"{len(mapping)} symbol rename(s)...",
        flush=True,
    )
    processed = 0
    for source in sources:
        destination = output_dir / source.name
        if source.suffix.lower() not in {".cpp", ".h", ".hpp"}:
            shutil.copy2(source, destination)
        else:
            text = source.read_text(encoding="utf-8")
            text = _replace_symbols(text, mapping, symbol_pattern)
            if source.name == "ps2_recompiled_functions.h":
                guard_namespace = re.sub(r"[^A-Za-z0-9_]", "_", namespace_name.upper())
                guard = f"OPENRATCHET_{guard_namespace}_PS2_RECOMPILED_FUNCTIONS_H"
                text = text.replace("PS2_RECOMPILED_FUNCTIONS_H", guard)
            destination.write_text(text, encoding="utf-8")
        processed += 1
        if processed == len(sources) or processed % 100 == 0:
            print(f"[overlay-aot] Finalized {processed}/{len(sources)} files", flush=True)

    table_header = output_dir / "openratchet_overlay_table.h"
    table_cpp = output_dir / "openratchet_overlay_table.cpp"
    table_header.write_text(
        "#pragma once\n\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n\n"
        "#include \"game/rac1_overlay_aot_dispatch.h\"\n\n"
        f"namespace {namespace_name} {{\n"
        "using FunctionEntry = ratchet::game::Rac1OverlayFunctionEntry;\n"
        + _materialized_range_header_block()
        + "extern const FunctionEntry kFunctions[];\n"
        "extern const std::size_t kFunctionCount;\n"
        f"inline constexpr std::uint32_t kGenerationEntry = 0x{generation_entry:08x}u;\n"
        f"}} // namespace {namespace_name}\n",
        encoding="utf-8",
    )
    lines = [
        '#include "openratchet_overlay_table.h"',
        '#include "ps2_recompiled_functions.h"',
        "",
        f"namespace {namespace_name} {{",
        _materialized_range_cpp_block(ranges).rstrip("\n"),
        "const FunctionEntry kFunctions[] = {",
    ]
    for pc, name in sorted(registrations):
        lines.append(f"    {{0x{pc:08x}u, &{mapping[name]}}},")
    lines.extend(
        [
            "};",
            "const std::size_t kFunctionCount = sizeof(kFunctions) / sizeof(kFunctions[0]);",
            f"}} // namespace {namespace_name}",
            "",
        ]
    )
    table_cpp.write_text("\n".join(lines), encoding="utf-8")

    return {
        "function_symbols": len(mapping),
        "registration_entries": len(registrations),
        "materialized_ranges": len(ranges),
        "materialized_slots": sum((end - begin) // 4 for begin, end in ranges),
        "first_pc": min(pc for pc, _ in registrations),
        "last_pc": max(pc for pc, _ in registrations),
    }


def _expand_boot_function_table_text(
    text: str, minimum_end: int
) -> tuple[str, dict[str, int]]:
    base_match = re.search(r"g_ps2RecompiledFunctionTableBase\s*=\s*0x([0-9a-fA-F]+)u", text)
    end_match = re.search(r"g_ps2RecompiledFunctionTableEnd\s*=\s*0x([0-9a-fA-F]+)u", text)
    count_match = re.search(r"g_ps2RecompiledFunctionTableSlotCount\s*=\s*(\d+)u", text)
    array_match = re.search(r"g_ps2RecompiledFunctionTable\[(\d+)u\]\s*=\s*\{\}", text)
    if not all((base_match, end_match, count_match, array_match)):
        raise OverlayAotError("cannot identify generated dense function table declarations")
    base = int(base_match.group(1), 16)
    old_end = int(end_match.group(1), 16)
    new_end = _align(max(old_end, minimum_end), 4)
    new_count = (new_end - base) // 4
    if new_count <= 0:
        raise OverlayAotError("expanded dense function table would be empty")

    text = re.sub(
        r"(g_ps2RecompiledFunctionTableEnd\s*=\s*)0x[0-9a-fA-F]+u",
        rf"\g<1>0x{new_end:x}u",
        text,
        count=1,
    )
    text = re.sub(
        r"(g_ps2RecompiledFunctionTableSlotCount\s*=\s*)\d+u",
        rf"\g<1>{new_count}u",
        text,
        count=1,
    )
    text = re.sub(
        r"(g_ps2RecompiledFunctionTable\[)\d+(u\]\s*=\s*\{\})",
        rf"\g<1>{new_count}\g<2>",
        text,
        count=1,
    )
    return text, {"base": base, "old_end": old_end, "new_end": new_end, "slot_count": new_count}


def expand_boot_function_table(register_path: Path, minimum_end: int) -> dict[str, int]:
    text = register_path.read_text(encoding="utf-8")
    expanded, summary = _expand_boot_function_table_text(text, minimum_end)
    _write_text_if_different(register_path, expanded)
    return summary


def _write_text_if_different(path: Path, text: str) -> bool:
    """Write text only when its UTF-8 content differs from the existing file."""
    if path.is_file():
        try:
            if path.read_text(encoding="utf-8") == text:
                return False
        except OSError as exc:
            raise OverlayAotError(f"cannot read existing output {path}: {exc}") from exc
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    except OSError as exc:
        raise OverlayAotError(f"cannot write output {path}: {exc}") from exc
    return True


def write_expanded_boot_function_table(
    source_path: Path, output_path: Path, minimum_end: int
) -> dict[str, int]:
    """Create a build-local dense boot table without dirtying generated/.

    Never shrink an already-expanded build-local table.  Individual generation
    codegen commands are intentionally usable outside ``build-native.ps1``; a
    later rebuild of a smaller generation (for example WAD158 after Level 0)
    must not truncate slots still required by another generated AOT module.
    Cleaning ``build/generated`` remains the explicit way to return to the boot
    table size.
    """
    try:
        text = source_path.read_text(encoding="utf-8")
    except OSError as exc:
        raise OverlayAotError(f"cannot read boot dispatch table {source_path}: {exc}") from exc

    preserved_end = minimum_end
    if output_path.is_file():
        try:
            existing = output_path.read_text(encoding="utf-8")
        except OSError as exc:
            raise OverlayAotError(f"cannot read existing build-local dispatch table {output_path}: {exc}") from exc
        match = re.search(r"g_ps2RecompiledFunctionTableEnd\s*=\s*0x([0-9a-fA-F]+)u", existing)
        if match is None:
            raise OverlayAotError(
                f"cannot identify existing build-local dispatch table end in {output_path}"
            )
        preserved_end = max(preserved_end, int(match.group(1), 16))

    expanded, summary = _expand_boot_function_table_text(text, preserved_end)
    _write_text_if_different(output_path, expanded)
    return summary


def _sha256_file(path: Path) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as exc:
        raise OverlayAotError(f"cannot hash {path}: {exc}") from exc


def _directory_hashes(root: Path, *, exclude_names: frozenset[str] = frozenset()) -> dict[str, str]:
    """Return a stable content manifest for every regular file below *root*.

    Paths are relative/POSIX so the fingerprint is independent of the checkout
    location.  Symlinks are rejected rather than silently fingerprinting a
    machine-specific target.
    """
    if not root.is_dir():
        raise OverlayAotError(f"directory to hash does not exist: {root}")
    result: dict[str, str] = {}
    try:
        paths = sorted(root.rglob("*"), key=lambda item: item.relative_to(root).as_posix())
    except OSError as exc:
        raise OverlayAotError(f"cannot enumerate {root}: {exc}") from exc
    for path in paths:
        if path.name in exclude_names:
            continue
        if path.is_symlink():
            raise OverlayAotError(f"refusing machine-dependent symlink in deterministic input: {path}")
        if not path.is_file():
            continue
        result[path.relative_to(root).as_posix()] = _sha256_file(path)
    return result


def _generated_outputs_match(generated_dir: Path, expected: object) -> bool:
    if not isinstance(expected, dict) or not expected:
        return False
    try:
        actual = _directory_hashes(
            generated_dir, exclude_names=frozenset({"openratchet_overlay_build.json"})
        )
    except OverlayAotError:
        return False
    return actual == expected


def _ghidra_fingerprint_inputs(ghidra_dir: Path) -> dict[str, object]:
    application_properties = ghidra_dir / "Ghidra" / "application.properties"
    if not application_properties.is_file():
        raise OverlayAotError(
            f"Ghidra application.properties not found at {application_properties}; "
            "-GhidraDir must name the installed Ghidra root"
        )

    result: dict[str, object] = {
        "root_name": ghidra_dir.name,
        "application_properties": _sha256_file(application_properties),
    }
    if os.name == "nt":
        appdata = os.environ.get("APPDATA")
        if not appdata:
            raise OverlayAotError("APPDATA is unavailable; cannot verify the EmotionEngine Ghidra extension")
        extension_dir = (
            Path(appdata)
            / "ghidra"
            / ghidra_dir.name
            / "Extensions"
            / "ghidra-emotionengine-reloaded"
        )
        extension_properties = extension_dir / "extension.properties"
        if not extension_properties.is_file():
            raise OverlayAotError(
                "ghidra-emotionengine-reloaded is required for deterministic PS2 overlay analysis; "
                f"expected {extension_properties}"
            )
        result["emotionengine_extension"] = _directory_hashes(extension_dir)
    return result


def _run_checked(command: Sequence[str], cwd: Path | None = None) -> None:
    printable = " ".join(str(part) for part in command)
    try:
        result = subprocess.run(list(command), cwd=cwd, check=False)
    except OSError as exc:
        raise OverlayAotError(f"cannot execute {command[0]}: {exc}") from exc
    if result.returncode != 0:
        raise OverlayAotError(f"command failed with exit code {result.returncode}: {printable}")


def _git_output(git: str, repo: Path, *args: str) -> str:
    try:
        result = subprocess.run(
            [git, "-C", str(repo), *args],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except OSError as exc:
        raise OverlayAotError(f"cannot execute git: {exc}") from exc
    if result.returncode != 0:
        raise OverlayAotError(
            f"git {' '.join(args)} failed for {repo}: {result.stderr.strip()}"
        )
    return result.stdout.strip()


def _prepare_generation_inputs(
    image: OverlayImage,
    work_dir: Path,
    elf_name: str,
) -> tuple[Path, Path, Path, dict[str, object]]:
    """Write deterministic ELF/config/manifest inputs for one Retail generation."""
    elf = build_overlay_elf(image)
    parsed = parse_elf_program_headers(elf)
    work_dir.mkdir(parents=True, exist_ok=True)
    elf_path = work_dir / elf_name
    raw_output = work_dir / "raw"
    config_path = work_dir / "ps2recomp.toml"
    manifest_path = work_dir / "manifest.json"
    if raw_output.exists():
        shutil.rmtree(raw_output)
    raw_output.mkdir(parents=True)
    elf_path.write_bytes(elf)
    write_ps2recomp_config(config_path, elf_path, raw_output)
    manifest = overlay_manifest(image, elf)
    manifest["elf"] = {
        "entry": f"0x{parsed['entry']:08x}",
        "program_headers": parsed["program_headers"],
    }
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return elf_path, raw_output, config_path, manifest


def ghidra_executable_range_args(image: OverlayImage) -> list[str]:
    executable = image.executable_segment
    return [f"0x{executable.destination:08x}:0x{executable.end:08x}"]


def ghidra_required_callable_entry_args(image: OverlayImage) -> list[str]:
    # Level 0 contains a consumer-proved callback registry at 0x1EA300.  Those
    # pointers are callable entry PCs even when Ghidra's automatic analysis does
    # not create Function objects for them.  Pass them to the exporter so Ghidra
    # itself derives any missing CFG bodies before PS2Recomp consumes the CSV.
    #
    # Keep these as opaque positional script arguments.  AnalyzeHeadless on Windows
    # has already proved that option-like ``--entry=...`` and punctuation-bearing
    # ``entry@...`` tokens are not transported byte-for-byte.  Use an alphanumeric
    # marker so CMD/Ghidra have nothing to reinterpret or escape.
    return [
        f"entry0x{pc:08x}"
        for pc in sorted(level0_registry_callback_targets(image))
    ]


def ghidra_export_script_args(image: OverlayImage, ghidra_csv: Path) -> list[str]:
    # Keep the Java exporter CLI and the Python build driver coupled in one tested
    # helper.  The exporter intentionally refuses to infer executable ranges from
    # Ghidra's global R5900 memory map because language-defined regions may be
    # executable but are not part of this Retail overlay generation.
    return [
        str(ghidra_csv),
        *ghidra_executable_range_args(image),
        *ghidra_required_callable_entry_args(image),
    ]


def stage_ghidra_export_script(export_script: Path, work_dir: Path) -> Path:
    """Stage a content-addressed Java script so Ghidra cannot reuse stale bytecode.

    Ghidra caches compiled Java scripts.  A ZIP extraction can restore source mtimes
    older than that cache, so timestamp-only invalidation is not reliable on Windows.
    The staged public class name is derived from the authoritative repo source hash:
    changed source therefore always means a different Java class/cache identity.
    """
    source = export_script.read_text(encoding="utf-8")
    protocol_marker = (
        'OPENRATCHET_EXPORTER_PROTOCOL = "' + GHIDRA_EXPORTER_PROTOCOL + '"'
    )
    if protocol_marker not in source:
        raise OverlayAotError(
            "repo-owned Ghidra exporter is stale or incompatible; expected protocol "
            + GHIDRA_EXPORTER_PROTOCOL
        )

    declaration = "public class ExportOpenRatchetFunctions extends GhidraScript"
    if source.count(declaration) != 1:
        raise OverlayAotError(
            "repo-owned Ghidra exporter must contain exactly one canonical public class declaration"
        )

    source_hash = hashlib.sha256(source.encode("utf-8")).hexdigest()
    class_name = f"ExportOpenRatchetFunctions_{source_hash[:16]}"
    staged_source = source.replace(
        declaration, f"public class {class_name} extends GhidraScript", 1
    )
    script_dir = work_dir / "ghidra-scripts"
    script_dir.mkdir(parents=True, exist_ok=True)
    staged_script = script_dir / f"{class_name}.java"
    staged_script.write_text(staged_source, encoding="utf-8", newline="\n")
    return staged_script


def build_overlay_aot(
    root: Path,
    generation: str,
    ghidra_dir: Path,
    ps2recomp_dir: Path,
    cmake: str = "cmake",
    git: str = "git",
    force: bool = False,
) -> dict[str, object]:
    """Generate and verify one proved Retail AOT generation deterministically.

    ``wad158`` is the early loader/game generation copied from ``wads/wad_158.wad``;
    ``level0`` is the controllable Veldin generation embedded in ``level_00.wad``.
    Both are consumed by the same Retail ``sub_0012D8F8`` generation boundary.
    """
    root = root.resolve()
    generation = generation.lower().replace("-", "").replace("_", "")

    toc = root / "build" / "toc.json"
    if generation == "wad158":
        generation_key = "wad_158"
        source_label = "wad_158.wad"
        source = root / "build" / "extracted" / "wads" / "wad_158.wad"
        namespace_name = "ratchet::generated::wad158"
        prefix = "rac1_w158_"
        project_name = "OpenRatchet_RAC1_WAD158_Overlay"
        elf_name = "wad_158_overlay.elf"
        work_name = "wad_158"
        generated_name = "wad_158"
        parse_inputs = [source]
    elif generation in {"level0", "0"}:
        generation_key = "level_00"
        source_label = "level_00.wad"
        source = root / "build" / "extracted" / "levels" / "level_00.wad"
        namespace_name = "ratchet::generated::level0"
        prefix = "rac1_l00_"
        project_name = "OpenRatchet_RAC1_L00_Overlay"
        elf_name = "level_00_overlay.elf"
        work_name = "level_00"
        generated_name = "level_00"
        parse_inputs = [toc, source]
    else:
        raise OverlayAotError(f"unsupported proved Retail generation: {generation}")

    tool = root / "tools" / "rac1_overlay_aot.py"
    revision_file = root / "patches" / "ps2recomp-base-revision.txt"
    patch_file = root / "patches" / "ps2recomp-synchronous-dmac-interrupt-stack.patch"
    prepare_tool = root / "cmake" / "PreparePs2RecompTool.cmake"
    boot_register_source = root / "generated" / "register_functions.cpp"
    build_generated_root = root / "build" / "generated"
    expanded_register_path = build_generated_root / "register_functions.cpp"
    export_script = root / "tools" / "ghidra" / "ExportOpenRatchetFunctions.java"
    analyze_headless = ghidra_dir / "support" / ("analyzeHeadless.bat" if os.name == "nt" else "analyzeHeadless")
    required = [*parse_inputs, tool, revision_file, patch_file, prepare_tool, boot_register_source, export_script]
    missing = [path for path in required if not path.is_file()]
    if missing:
        raise OverlayAotError("missing overlay-AOT build input(s): " + ", ".join(str(path) for path in missing))

    source_bytes = source.read_bytes()
    if generation_key == "wad_158":
        image = parse_raw_overlay_container(source_bytes, 158)
    else:
        location = load_level_location(toc, 0)
        image = parse_level_overlay(source_bytes, location)

    expected_revision = revision_file.read_text(encoding="utf-8").strip()
    actual_revision = _git_output(git, ps2recomp_dir, "rev-parse", "HEAD")
    if actual_revision != expected_revision:
        raise OverlayAotError(
            f"unsupported PS2Recomp revision {actual_revision}; expected {expected_revision}"
        )
    status = _git_output(git, ps2recomp_dir, "status", "--porcelain", "--untracked-files=all")
    if status:
        raise OverlayAotError("third_party/PS2Recomp must remain clean before overlay AOT generation")

    work_dir = root / "build" / "tooling" / "overlay-aot" / work_name
    generated_dir = build_generated_root / "overlays" / generated_name
    build_state = generated_dir / "openratchet_overlay_build.json"
    raw_dir = work_dir / "raw"
    elf_path = work_dir / elf_name
    ghidra_csv = work_dir / "ghidra.csv"
    aot_csv = work_dir / "ghidra-aot.csv"
    config_path = work_dir / "ps2recomp.toml"

    if not analyze_headless.is_file():
        raise OverlayAotError(
            f"Ghidra headless analyzer not found at {analyze_headless}; "
            "set -GhidraDir or OPENRATCHET_GHIDRA_DIR to the installed Ghidra root"
        )

    fingerprint_inputs: dict[str, object] = {
        "generation": generation_key,
        "source": _sha256_file(source),
        "tool": _sha256_file(tool),
        "patch": _sha256_file(patch_file),
        "exporter": _sha256_file(export_script),
        "ghidra": _ghidra_fingerprint_inputs(ghidra_dir),
        "ps2recomp_revision": expected_revision,
        "schema": "rac1-overlay-aot-v8",
    }
    if generation_key == "level_00":
        fingerprint_inputs["toc"] = _sha256_file(toc)
    fingerprint = hashlib.sha256(
        json.dumps(fingerprint_inputs, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()

    cached = False
    metadata_upgrade = False
    prior: dict[str, object] = {}
    if not force and build_state.is_file():
        try:
            loaded = json.loads(build_state.read_text(encoding="utf-8"))
            if isinstance(loaded, dict):
                prior = loaded
                generated_match = _generated_outputs_match(
                    generated_dir, prior.get("generated_outputs")
                )
                cached = prior.get("fingerprint") == fingerprint and generated_match
                prior_inputs = prior.get("fingerprint_inputs")
                if not cached and generated_match and isinstance(prior_inputs, dict):
                    # v7 -> v8 changes only dispatch ownership metadata. Every
                    # expensive semantic input must still match byte-for-byte;
                    # only the old all-in-one tool hash and explicit schema tag
                    # may differ for this one migration.
                    metadata_upgrade = _v7_metadata_upgrade_compatible(
                        prior_inputs, fingerprint_inputs
                    )
        except (OSError, json.JSONDecodeError):
            cached = False
            metadata_upgrade = False

    if metadata_upgrade:
        ranges = tuple((segment.destination, segment.end) for segment in image.segments)
        _upgrade_overlay_materialized_range_metadata(
            generated_dir, namespace_name, ranges
        )
        finalized = prior.get("finalized")
        if not isinstance(finalized, dict):
            finalized = {}
        finalized = dict(finalized)
        finalized["materialized_ranges"] = len(ranges)
        finalized["materialized_slots"] = sum((end - begin) // 4 for begin, end in ranges)
        prior["schema"] = 7
        prior["fingerprint"] = fingerprint
        prior["fingerprint_inputs"] = fingerprint_inputs
        prior["finalized"] = finalized
        prior["generated_outputs"] = _directory_hashes(
            generated_dir, exclude_names=frozenset({"openratchet_overlay_build.json"})
        )
        build_state.write_text(
            json.dumps(prior, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        manifest = (
            json.loads((work_dir / "manifest.json").read_text(encoding="utf-8"))
            if (work_dir / "manifest.json").is_file() else {}
        )
        expansion = write_expanded_boot_function_table(
            boot_register_source, expanded_register_path, image.executable_segment.end
        )
        return {
            "generation": generation_key,
            "source": source_label,
            "generation_entry": f"0x{image.generation_entry:08x}",
            "fingerprint": fingerprint,
            "cache": "metadata-upgrade",
            "dispatch_table_end": f"0x{expansion['new_end']:08x}",
            "manifest": manifest,
        }

    if cached:
        manifest = json.loads((work_dir / "manifest.json").read_text(encoding="utf-8")) if (work_dir / "manifest.json").is_file() else {}
        executable_end = int(str(prior["executable_end"]), 0)
        expansion = write_expanded_boot_function_table(
            boot_register_source, expanded_register_path, executable_end
        )
        return {
            "generation": generation_key,
            "source": source_label,
            "generation_entry": prior["generation_entry"],
            "fingerprint": fingerprint,
            "cache": "hit",
            "dispatch_table_end": f"0x{expansion['new_end']:08x}",
            "manifest": manifest,
        }

    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True, exist_ok=True)
    _prepare_generation_inputs(image, work_dir, elf_name)
    staged_export_script = stage_ghidra_export_script(export_script, work_dir)

    project_dir = work_dir / "ghidra-project"
    project_dir.mkdir(parents=True, exist_ok=True)
    _run_checked(
        [
            str(analyze_headless),
            str(project_dir),
            project_name,
            "-import",
            str(elf_path),
            "-overwrite",
            "-scriptPath",
            str(staged_export_script.parent),
            "-postScript",
            staged_export_script.name,
            *ghidra_export_script_args(image, ghidra_csv),
            "-deleteProject",
        ]
    )
    # Raw Ghidra analysis owns function boundaries, but it may absorb a real Retail
    # call destination into a larger Function object.  Validate its ranges first,
    # then add only WAD/consumer-proved alternate entries inside those ranges and
    # require the augmented map to cover every direct JAL target.
    ghidra_summary = validate_ghidra_map(
        image, ghidra_csv, require_direct_jal_targets=False
    )
    augmentation_summary = augment_ghidra_map_with_required_entries(image, ghidra_csv, aot_csv)
    aot_ghidra_summary = validate_ghidra_map(image, aot_csv)
    write_ps2recomp_config(config_path, elf_path, raw_dir, aot_csv)

    prepared_source = root / "build" / "tooling" / "_openratchet" / "PS2Recomp"
    _run_checked(
        [
            cmake,
            f"-DOPENRATCHET_PS2RECOMP_UPSTREAM={ps2recomp_dir}",
            f"-DOPENRATCHET_PS2RECOMP_PATCH={patch_file}",
            f"-DOPENRATCHET_PS2RECOMP_REVISION={revision_file}",
            f"-DOPENRATCHET_PS2RECOMP_OUTPUT={prepared_source}",
            "-P",
            str(prepare_tool),
        ],
        cwd=root,
    )
    tool_build = root / "build" / "tooling" / "ps2recomp"
    _run_checked(
        [
            cmake,
            "-S",
            str(prepared_source),
            "-B",
            str(tool_build),
            "-DPS2X_BUILD_RUNTIME=OFF",
            "-DPS2X_BUILD_ANALYZER=OFF",
            "-DPS2X_BUILD_TEST=OFF",
            "-DPS2X_BUILD_STUDIO=OFF",
        ],
        cwd=root,
    )
    _run_checked(
        [cmake, "--build", str(tool_build), "--config", "Release", "--target", "ps2_recomp", "--parallel"],
        cwd=root,
    )
    candidates = [
        tool_build / "ps2xRecomp" / "Release" / "ps2_recomp.exe",
        tool_build / "ps2xRecomp" / "ps2_recomp",
        tool_build / "ps2_recomp",
    ]
    recompiler = next((candidate for candidate in candidates if candidate.is_file()), None)
    if recompiler is None:
        raise OverlayAotError("built PS2Recomp executable was not found")
    _run_checked([str(recompiler), str(config_path)], cwd=root)

    print("[overlay-aot] Verifying PS2Recomp dispatch coverage...", flush=True)
    output_summary = verify_ps2recomp_output(image, aot_csv, raw_dir)
    print(
        f"[overlay-aot] Dispatch coverage OK: {output_summary['registered_entries']} registered entries",
        flush=True,
    )
    finalize_summary = finalize_overlay_module(
        raw_dir,
        generated_dir,
        prefix,
        namespace_name,
        image.generation_entry,
        tuple((segment.destination, segment.end) for segment in image.segments),
    )
    expansion = write_expanded_boot_function_table(
        boot_register_source, expanded_register_path, image.executable_segment.end
    )
    generated_outputs = _directory_hashes(
        generated_dir, exclude_names=frozenset({"openratchet_overlay_build.json"})
    )
    if not generated_outputs:
        raise OverlayAotError("finalized overlay AOT module unexpectedly contains no generated files")
    state = {
        "schema": 7,
        "kind": "OPENRATCHET_RAC1_OVERLAY_AOT_BUILD",
        "generation": generation_key,
        "source": source_label,
        "fingerprint": fingerprint,
        "fingerprint_inputs": fingerprint_inputs,
        "generation_entry": f"0x{image.generation_entry:08x}",
        "executable_start": f"0x{image.executable_segment.destination:08x}",
        "executable_end": f"0x{image.executable_segment.end:08x}",
        "ghidra": ghidra_summary,
        "ghidra_aot": aot_ghidra_summary,
        "ghidra_augmentation": augmentation_summary,
        "ps2recomp": output_summary,
        "finalized": finalize_summary,
        "generated_outputs": generated_outputs,
        "dispatch_table_end": f"0x{expansion['new_end']:08x}",
    }
    build_state.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return {
        "generation": generation_key,
        "source": source_label,
        "generation_entry": state["generation_entry"],
        "fingerprint": fingerprint,
        "cache": "miss",
        "ghidra_records": ghidra_summary["records"],
        "synthetic_registry_entries": augmentation_summary["synthetic_alternate_entries"],
        "registered_entries": output_summary["registered_entries"],
        "resume_entries": output_summary["resume_entries"],
        "dispatch_table_end": state["dispatch_table_end"],
    }


def _parse_int(value: str) -> int:
    return int(value, 0)


def _parse_materialized_range(value: str) -> tuple[int, int]:
    try:
        begin_text, end_text = value.split(":", 1)
        return int(begin_text, 0), int(end_text, 0)
    except (ValueError, TypeError) as exc:
        raise argparse.ArgumentTypeError(
            f"expected materialized range START:END, got {value!r}"
        ) from exc


def _main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)

    prepare = sub.add_parser("prepare", help="extract a level overlay and create a minimal MIPS ELF")
    prepare.add_argument("--toc", type=Path, required=True)
    prepare.add_argument("--wad", type=Path, required=True)
    prepare.add_argument("--level-id", type=int, default=0)
    prepare.add_argument("--work-dir", type=Path, required=True)
    prepare.add_argument("--oracle-ram", type=Path)

    finalize = sub.add_parser("finalize", help="namespace a secondary PS2Recomp output")
    finalize.add_argument("--raw-dir", type=Path, required=True)
    finalize.add_argument("--output-dir", type=Path, required=True)
    finalize.add_argument("--prefix", default="rac1_l0_")
    finalize.add_argument("--namespace", default="ratchet::generated::level0")
    finalize.add_argument("--generation-entry", type=_parse_int, required=True)
    finalize.add_argument(
        "--materialized-range",
        type=_parse_materialized_range,
        action="append",
        required=True,
        help="exact Retail record destination interval START:END (repeat for every record)",
    )

    expand = sub.add_parser("expand-table", help="expand boot dense dispatch storage for overlay PCs")
    expand.add_argument("--register", type=Path, required=True)
    expand.add_argument("--minimum-end", type=_parse_int, required=True)

    verify = sub.add_parser("verify", help="prove Ghidra and PS2Recomp coverage for an overlay generation")
    verify.add_argument("--toc", type=Path, required=True)
    verify.add_argument("--wad", type=Path, required=True)
    verify.add_argument("--level-id", type=int, default=0)
    verify.add_argument("--ghidra-csv", type=Path, required=True)
    verify.add_argument("--raw-dir", type=Path, required=True)

    build = sub.add_parser("build", help="headlessly generate and verify one proved Retail overlay AOT module")
    build.add_argument("--root", type=Path, required=True)
    build.add_argument("--generation", choices=("wad158", "level0"), default="level0")
    build.add_argument("--ghidra-dir", type=Path, required=True)
    build.add_argument("--ps2recomp-dir", type=Path)
    build.add_argument("--cmake", default="cmake")
    build.add_argument("--git", default="git")
    build.add_argument("--force", action="store_true")

    args = parser.parse_args(list(argv) if argv is not None else None)
    try:
        if args.command == "prepare":
            result = prepare_overlay_aot(args.toc, args.wad, args.level_id, args.work_dir, args.oracle_ram)
        elif args.command == "finalize":
            result = finalize_overlay_module(
                args.raw_dir,
                args.output_dir,
                args.prefix,
                args.namespace,
                args.generation_entry,
                args.materialized_range,
            )
        elif args.command == "expand-table":
            result = expand_boot_function_table(args.register, args.minimum_end)
        elif args.command == "verify":
            location = load_level_location(args.toc, args.level_id)
            image = parse_level_overlay(args.wad.read_bytes(), location)
            result = verify_ps2recomp_output(image, args.ghidra_csv, args.raw_dir)
        else:
            ps2recomp_dir = args.ps2recomp_dir or (args.root / "third_party" / "PS2Recomp")
            result = build_overlay_aot(
                args.root,
                args.generation,
                args.ghidra_dir,
                ps2recomp_dir,
                args.cmake,
                args.git,
                force=args.force,
            )
    except OverlayAotError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
