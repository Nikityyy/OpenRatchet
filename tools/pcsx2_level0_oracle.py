#!/usr/bin/env python3
"""One-shot PCSX2 Retail oracle capture for R&C1 Level 0 (Veldin).

This is investigation tooling only. It never patches OpenRatchet runtime state and
never writes guest game memory. PCSX2 is used strictly as a Retail reference
oracle. The tool captures one reproducible baseline plus deterministic physical-input
windows, derives writer-watchpoint targets from actual input-vs-idle RAM differences,
and stores sparse full-EE-RAM deltas in one ZIP for differential analysis.

Requirements:
  * PCSX2-MCP style DebugServer on 127.0.0.1:21512.
  * PCSX2 Pine IPC enabled on 127.0.0.1:28011 for save/load-state replay.
  * R&C1 NTSC-U at normal controllable Level-0/Veldin gameplay.

No controller state is injected into guest RAM. Scenario input is supplied by
holding the requested real PCSX2 control while Retail runs a fixed capture window.
The live Level-0 overlay fingerprint is the code-generation gate; stale boot-ELF
frame addresses are intentionally not used by the active capture path.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zipfile
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

# Reuse the already-tested DebugServer transport owned by OpenRatchet tooling.
try:
    from pcsx2_sif_capture import CaptureError, DebugServerClient, format_address, parse_address
except ImportError:  # pragma: no cover - direct import diagnostics
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from pcsx2_sif_capture import CaptureError, DebugServerClient, format_address, parse_address

SCHEMA = 1
EE_RAM_BYTES = 0x02000000
READ_CHUNK_BYTES = 0x10000  # DebugServer hard cap.
PAGE_BYTES = 0x1000

# Boot-ELF checkpoints retained only as historical/diagnostic helpers.  Retail
# Level-0 gameplay overwrites this address range with the level overlay, so the
# capture path below MUST NOT use these addresses as gameplay checkpoints.
FRAME_PRE_PC = 0x001EBA08
FRAME_PRE_OPCODE = 0x0C085E84
FRAME_POST_INPUT_PC = 0x001EBA10
FRAME_END_PC = 0x001EBC24

# Exact bytes from the authoritative Level-0 overlay segment whose destination is
# 0x001EAA00.  PCSX2 Retail at controllable Veldin must contain this generation.
# The previous oracle treated the differing boot-ELF opcode as a version mismatch;
# that was wrong.  The difference is the evidence that the gameplay overlay is live.
LEVEL0_OVERLAY_PROBE_PC = 0x001EBA08
LEVEL0_OVERLAY_PROBE_BYTES = bytes.fromhex(
    "09f840002d20a0000000bf7b0800e0031000bd2700000000a0ffbd271600023c"
)
BOOT_ELF_PROBE_BYTES = bytes.fromhex(
    "845e080c00000000d4ab070c8881848f1800033c1600013c18f620acb062628c"
)
DEFAULT_SCENARIO_RUN_MS = 500
DEFAULT_WRITER_PAGE_LIMIT = 24
DEFAULT_WRITER_EVENT_LIMIT = 96

# Proven boot-generation semantic anchors are retained for offline compatibility
# tests only. They are intentionally NOT capture gates once the Level-0 overlay is
# resident, because that code generation owns different live globals/layouts.
CONTROLLER_STATE = 0x0013C940
CONTROLLER_BYTES = 0x300
CAMERA_STATE = 0x00186F40
CAMERA_BYTES = 0x400
MOBY_POOL_PTR = 0x0015FF18
MOBY_LAST_PTR = 0x0015FF20
MOBY_RECORD_BYTES = 0x100
MOBY_POOL_BYTES = 0x4000
MOBY_TRAVERSAL_OFFSET = 0x20
MOBY_PVAR_OFFSET = 0x78
MOBY_OCLASS_OFFSET = 0xA6
RATCHET_STATE_RATCHET_OFFSET = 0x44

DEFAULT_SCENARIOS: tuple[tuple[str, str], ...] = (
    ("idle", "Release every movement/camera/action control."),
    ("move_forward", "Hold FORWARD for Ratchet."),
    ("move_backward", "Hold BACKWARD for Ratchet."),
    ("move_left", "Hold LEFT movement for Ratchet."),
    ("move_right", "Hold RIGHT movement for Ratchet."),
    ("camera_left", "Hold CAMERA LEFT / right-stick left."),
    ("camera_right", "Hold CAMERA RIGHT / right-stick right."),
    ("camera_up", "Hold CAMERA UP / right-stick up."),
    ("camera_down", "Hold CAMERA DOWN / right-stick down."),
    ("jump", "Hold/press JUMP and keep it held until capture finishes."),
    ("attack", "Hold/press the normal ATTACK control and keep it held until capture finishes."),
)


class OracleError(RuntimeError):
    pass


@dataclass(slots=True)
class PineClient:
    host: str = "127.0.0.1"
    port: int = 28011
    timeout: float = 5.0
    sock: socket.socket | None = field(default=None, init=False, repr=False)

    MSG_VERSION = 0x08
    MSG_SAVE_STATE = 0x09
    MSG_LOAD_STATE = 0x0A
    MSG_TITLE = 0x0B
    MSG_ID = 0x0C
    MSG_UUID = 0x0D
    MSG_GAME_VERSION = 0x0E
    MSG_STATUS = 0x0F

    def connect(self) -> None:
        if self.sock is not None:
            return
        try:
            sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
            sock.settimeout(self.timeout)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.sock = sock
        except OSError as exc:
            raise OracleError(
                f"Could not connect to PCSX2 Pine IPC at {self.host}:{self.port}. "
                "Enable Pine IPC in PCSX2 Settings -> Advanced. "
                f"Original error: {exc}"
            ) from exc

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def __enter__(self) -> "PineClient":
        self.connect()
        return self

    def __exit__(self, _exc_type: object, _exc: object, _tb: object) -> None:
        self.close()

    def _recv_exact(self, size: int) -> bytes:
        assert self.sock is not None
        out = bytearray()
        while len(out) < size:
            chunk = self.sock.recv(size - len(out))
            if not chunk:
                raise OracleError("PCSX2 Pine IPC closed the connection unexpectedly.")
            out.extend(chunk)
        return bytes(out)

    def request(self, payload: bytes) -> bytes:
        self.connect()
        assert self.sock is not None
        packet = struct.pack("<I", len(payload) + 4) + payload
        self.sock.sendall(packet)
        size = struct.unpack("<I", self._recv_exact(4))[0]
        if size < 5 or size > 1_000_000:
            raise OracleError(f"Invalid Pine response size: {size}")
        response = self._recv_exact(size - 4)
        if not response or response[0] != 0:
            code = response[0] if response else 0xFF
            raise OracleError(f"Pine command 0x{payload[0]:02x} failed with code 0x{code:02x}.")
        return response

    def _get_string(self, command: int) -> str:
        response = self.request(bytes([command]))
        if len(response) < 5:
            return ""
        length = struct.unpack_from("<I", response, 1)[0]
        raw = response[5:5 + length]
        return raw.rstrip(b"\0").decode("utf-8", errors="replace")

    def version(self) -> str:
        return self._get_string(self.MSG_VERSION)

    def title(self) -> str:
        return self._get_string(self.MSG_TITLE)

    def game_id(self) -> str:
        return self._get_string(self.MSG_ID)

    def uuid(self) -> str:
        return self._get_string(self.MSG_UUID)

    def game_version(self) -> str:
        return self._get_string(self.MSG_GAME_VERSION)

    def status(self) -> int:
        response = self.request(bytes([self.MSG_STATUS]))
        if len(response) < 5:
            raise OracleError("Pine status response is truncated.")
        return struct.unpack_from("<I", response, 1)[0]

    def save_state(self, slot: int) -> None:
        if not 0 <= slot <= 9:
            raise OracleError("Savestate slot must be 0..9.")
        self.request(bytes([self.MSG_SAVE_STATE, slot]))

    def load_state(self, slot: int) -> None:
        if not 0 <= slot <= 9:
            raise OracleError("Savestate slot must be 0..9.")
        self.request(bytes([self.MSG_LOAD_STATE, slot]))


class OracleDebugClient(DebugServerClient):
    def pause(self, cpu: str = "ee") -> dict[str, Any]:
        return self.request("pause", cpu=cpu)

    def disassemble(self, cpu: str, address: int, count: int = 12) -> list[dict[str, Any]]:
        response = self.request(
            "disassemble", cpu=cpu, address=format_address(address), count=count, simplify=True
        )
        return list(response.get("instructions", []))

    def backtrace(self, cpu: str, max_frames: int = 32) -> dict[str, Any]:
        return self.request("get_backtrace", cpu=cpu, max_frames=max_frames)

    def set_memcheck(
        self,
        cpu: str,
        start: int,
        end: int,
        description: str,
        *,
        kind: str = "write",
    ) -> None:
        self.request(
            "set_memcheck",
            cpu=cpu,
            address=format_address(start),
            end=format_address(end),
            type=kind,
            action="break",
            condition="",
            description=description,
        )

    def remove_memcheck(self, cpu: str, start: int, end: int) -> None:
        self.request(
            "remove_memcheck", cpu=cpu, address=format_address(start), end=format_address(end)
        )

    def list_memchecks(self, cpu: str) -> list[dict[str, Any]]:
        return list(self.request("list_memchecks", cpu=cpu).get("memchecks", []))


@dataclass(slots=True)
class WatchRange:
    name: str
    start: int
    end: int


@dataclass(slots=True)
class SemanticState:
    controller_hex: str
    camera_hex: str
    moby_pool_base: int
    moby_pool_last: int
    ratchet_moby: int
    ratchet_pvar: int
    ratchet_state: int
    ratchet_record_hex: str
    ratchet_pvar_hex: str
    ratchet_state_hex: str

    def as_json(self) -> dict[str, Any]:
        return {
            "controller": {"address": format_address(CONTROLLER_STATE), "hex": self.controller_hex},
            "camera": {"address": format_address(CAMERA_STATE), "hex": self.camera_hex},
            "moby_pool": {
                "base": format_address(self.moby_pool_base),
                "last": format_address(self.moby_pool_last),
            },
            "ratchet": {
                "moby": format_address(self.ratchet_moby),
                "pvar": format_address(self.ratchet_pvar),
                "state": format_address(self.ratchet_state),
                "record_hex": self.ratchet_record_hex,
                "pvar_hex": self.ratchet_pvar_hex,
                "state_hex": self.ratchet_state_hex,
            },
        }


def le_u16(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 2], "little")


def le_u32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 4], "little")


def signed8(value: int) -> int:
    return value - 256 if value >= 128 else value


def find_ratchet_from_pool(ram: bytes) -> tuple[int, int, int, int, int]:
    if len(ram) < EE_RAM_BYTES:
        raise OracleError("EE RAM snapshot is truncated.")
    base = le_u32(ram, MOBY_POOL_PTR)
    last = le_u32(ram, MOBY_LAST_PTR)
    if base == 0 or base + MOBY_POOL_BYTES > len(ram):
        raise OracleError(f"Retail Moby pool is not materialized (base={format_address(base)}).")
    expected_last = base + 0x3F00
    if last != expected_last:
        raise OracleError(
            f"Retail Moby pool last-slot mismatch: got {format_address(last)}, "
            f"expected {format_address(expected_last)}."
        )
    ratchets: list[int] = []
    for slot in range(64):
        addr = base + slot * MOBY_RECORD_BYTES
        traversal = signed8(ram[addr + MOBY_TRAVERSAL_OFFSET])
        if traversal == -1:
            break
        if traversal < 0:
            continue
        oclass = le_u16(ram, addr + MOBY_OCLASS_OFFSET)
        if oclass == 0:
            ratchets.append(addr)
    if len(ratchets) != 1:
        raise OracleError(f"Expected one live oClass=0 Ratchet, found {len(ratchets)}.")
    ratchet = ratchets[0]
    pvar = le_u32(ram, ratchet + MOBY_PVAR_OFFSET)
    if not (0 < pvar <= len(ram) - 4):
        pvar = 0
    state = le_u32(ram, pvar) if pvar else 0
    if state >= len(ram):
        state = 0
    return base, last, ratchet, pvar, state


def read_region_from_snapshot(ram: bytes, address: int, length: int) -> bytes:
    if address == 0 or address >= len(ram):
        return b""
    return ram[address:min(address + length, len(ram))]


def semantic_from_ram(ram: bytes) -> SemanticState:
    base, last, ratchet, pvar, state = find_ratchet_from_pool(ram)
    return SemanticState(
        controller_hex=read_region_from_snapshot(ram, CONTROLLER_STATE, CONTROLLER_BYTES).hex(),
        camera_hex=read_region_from_snapshot(ram, CAMERA_STATE, CAMERA_BYTES).hex(),
        moby_pool_base=base,
        moby_pool_last=last,
        ratchet_moby=ratchet,
        ratchet_pvar=pvar,
        ratchet_state=state,
        ratchet_record_hex=read_region_from_snapshot(ram, ratchet, MOBY_RECORD_BYTES).hex(),
        ratchet_pvar_hex=read_region_from_snapshot(ram, pvar, 0x1000).hex() if pvar else "",
        ratchet_state_hex=read_region_from_snapshot(ram, state, 0x400).hex() if state else "",
    )


def build_watch_ranges(semantic: SemanticState) -> list[WatchRange]:
    ranges = [
        WatchRange("controller", CONTROLLER_STATE, CONTROLLER_STATE + CONTROLLER_BYTES),
        WatchRange("camera", CAMERA_STATE, CAMERA_STATE + CAMERA_BYTES),
        WatchRange("ratchet-moby", semantic.ratchet_moby, semantic.ratchet_moby + MOBY_RECORD_BYTES),
    ]
    if semantic.ratchet_pvar:
        ranges.append(WatchRange("ratchet-pvar", semantic.ratchet_pvar, semantic.ratchet_pvar + 0x1000))
    if semantic.ratchet_state:
        ranges.append(WatchRange("ratchet-state", semantic.ratchet_state, semantic.ratchet_state + 0x400))
    # Clip to EE RAM and remove duplicates/contained ranges.
    clipped: list[WatchRange] = []
    for item in ranges:
        start = max(0, min(item.start, EE_RAM_BYTES))
        end = max(start, min(item.end, EE_RAM_BYTES))
        if end <= start:
            continue
        if any(start >= other.start and end <= other.end for other in clipped):
            continue
        clipped.append(WatchRange(item.name, start, end))
    return clipped


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_dump(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")




def _pe_rva_to_file_offset(data: bytes, rva: int, sections: list[tuple[int, int, int, int]]) -> int:
    """Map a PE RVA to a file offset using section virtual/raw ranges."""
    for virtual_address, virtual_size, raw_pointer, raw_size in sections:
        span = max(virtual_size, raw_size)
        if virtual_address <= rva < virtual_address + span:
            offset = raw_pointer + (rva - virtual_address)
            if 0 <= offset < len(data):
                return offset
    if 0 <= rva < len(data):
        return rva
    raise OracleError(f"PE RVA 0x{rva:x} is outside the image.")


def pe_export_rva(image_path: Path, symbol: str) -> int:
    """Return the RVA of an exported PE symbol without external dependencies."""
    data = image_path.read_bytes()
    if len(data) < 0x100 or data[:2] != b"MZ":
        raise OracleError(f"PCSX2 image is not a valid PE file: {image_path}")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe + 24 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise OracleError(f"PCSX2 image has no valid PE header: {image_path}")
    number_of_sections = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic == 0x20B:  # PE32+
        data_directory = optional + 112
    elif magic == 0x10B:  # PE32
        data_directory = optional + 96
    else:
        raise OracleError(f"Unsupported PCSX2 PE optional-header magic 0x{magic:x}.")
    export_rva, export_size = struct.unpack_from("<II", data, data_directory)
    if not export_rva:
        raise OracleError("PCSX2 executable has no export directory.")

    sections: list[tuple[int, int, int, int]] = []
    section_base = optional + optional_size
    for index in range(number_of_sections):
        off = section_base + index * 40
        if off + 40 > len(data):
            raise OracleError("PCSX2 PE section table is truncated.")
        virtual_size = struct.unpack_from("<I", data, off + 8)[0]
        virtual_address = struct.unpack_from("<I", data, off + 12)[0]
        raw_size = struct.unpack_from("<I", data, off + 16)[0]
        raw_pointer = struct.unpack_from("<I", data, off + 20)[0]
        sections.append((virtual_address, virtual_size, raw_pointer, raw_size))

    export_off = _pe_rva_to_file_offset(data, export_rva, sections)
    if export_off + 40 > len(data):
        raise OracleError("PCSX2 PE export directory is truncated.")
    fields = struct.unpack_from("<IIHHIIIIIII", data, export_off)
    function_count = fields[6]
    name_count = fields[7]
    functions_rva = fields[8]
    names_rva = fields[9]
    ordinals_rva = fields[10]
    functions_off = _pe_rva_to_file_offset(data, functions_rva, sections)
    names_off = _pe_rva_to_file_offset(data, names_rva, sections)
    ordinals_off = _pe_rva_to_file_offset(data, ordinals_rva, sections)

    for index in range(name_count):
        name_rva = struct.unpack_from("<I", data, names_off + index * 4)[0]
        name_off = _pe_rva_to_file_offset(data, name_rva, sections)
        end = data.find(b"\0", name_off)
        if end < 0:
            continue
        try:
            current = data[name_off:end].decode("ascii")
        except UnicodeDecodeError:
            continue
        if current != symbol:
            continue
        ordinal = struct.unpack_from("<H", data, ordinals_off + index * 2)[0]
        if ordinal >= function_count:
            raise OracleError(f"PCSX2 export {symbol} has invalid ordinal index {ordinal}.")
        return struct.unpack_from("<I", data, functions_off + ordinal * 4)[0]
    raise OracleError(f"PCSX2 executable does not export {symbol}.")


@dataclass(slots=True)
class WindowsDirectEERamReader:
    """Read PCSX2 EE RAM directly from its exported HostMemoryMap::EEmem pointer.

    PCSX2 intentionally exports EEmem for debuggers on Windows. This path is read-only
    and is used only while the EE is paused. It avoids 512 JSON/TCP requests per 32 MiB
    snapshot, while DebugServer remains the control/watchpoint oracle.
    """

    pid: int
    process_handle: Any
    ee_host_address: int
    image_path: Path

    @classmethod
    def open(cls, pid: int) -> "WindowsDirectEERamReader":
        if os.name != "nt":
            raise OracleError("Direct PCSX2 EE-RAM access is available only on Windows.")
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel32.OpenProcess.restype = wintypes.HANDLE
        kernel32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
        kernel32.QueryFullProcessImageNameW.restype = wintypes.BOOL
        kernel32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
        kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel32.CloseHandle.restype = wintypes.BOOL
        kernel32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        kernel32.ReadProcessMemory.restype = wintypes.BOOL
        PROCESS_VM_READ = 0x0010
        PROCESS_QUERY_INFORMATION = 0x0400
        handle = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
        if not handle:
            raise OracleError(f"Could not open PCSX2 process {pid} for read-only memory access (Win32 {ctypes.get_last_error()}).")

        try:
            capacity = wintypes.DWORD(32768)
            path_buf = ctypes.create_unicode_buffer(capacity.value)
            if not kernel32.QueryFullProcessImageNameW(handle, 0, path_buf, ctypes.byref(capacity)):
                raise OracleError(f"Could not resolve PCSX2 executable path (Win32 {ctypes.get_last_error()}).")
            image_path = Path(path_buf.value)

            TH32CS_SNAPMODULE = 0x00000008
            TH32CS_SNAPMODULE32 = 0x00000010
            INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

            class MODULEENTRY32W(ctypes.Structure):
                _fields_ = [
                    ("dwSize", wintypes.DWORD),
                    ("th32ModuleID", wintypes.DWORD),
                    ("th32ProcessID", wintypes.DWORD),
                    ("GlblcntUsage", wintypes.DWORD),
                    ("ProccntUsage", wintypes.DWORD),
                    ("modBaseAddr", ctypes.POINTER(ctypes.c_ubyte)),
                    ("modBaseSize", wintypes.DWORD),
                    ("hModule", wintypes.HMODULE),
                    ("szModule", wintypes.WCHAR * 256),
                    ("szExePath", wintypes.WCHAR * 260),
                ]

            kernel32.Module32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(MODULEENTRY32W)]
            kernel32.Module32FirstW.restype = wintypes.BOOL
            snapshot = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
            if snapshot == INVALID_HANDLE_VALUE:
                raise OracleError(f"Could not enumerate PCSX2 modules (Win32 {ctypes.get_last_error()}).")
            try:
                entry = MODULEENTRY32W()
                entry.dwSize = ctypes.sizeof(entry)
                if not kernel32.Module32FirstW(snapshot, ctypes.byref(entry)):
                    raise OracleError(f"Could not read PCSX2 main module (Win32 {ctypes.get_last_error()}).")
                module_base = ctypes.cast(entry.modBaseAddr, ctypes.c_void_p).value or 0
            finally:
                kernel32.CloseHandle(snapshot)

            eemem_rva = pe_export_rva(image_path, "EEmem")
            pointer_address = module_base + eemem_rva
            raw_ptr = (ctypes.c_ubyte * ctypes.sizeof(ctypes.c_void_p))()
            read = ctypes.c_size_t()
            if not kernel32.ReadProcessMemory(
                handle,
                ctypes.c_void_p(pointer_address),
                ctypes.cast(raw_ptr, ctypes.c_void_p),
                ctypes.sizeof(raw_ptr),
                ctypes.byref(read),
            ) or read.value != ctypes.sizeof(raw_ptr):
                raise OracleError(f"Could not read exported PCSX2 EEmem pointer (Win32 {ctypes.get_last_error()}).")
            ee_host_address = int.from_bytes(bytes(raw_ptr), "little")
            if ee_host_address < 0x10000:
                raise OracleError(f"PCSX2 EEmem export is not initialized (0x{ee_host_address:x}).")
            return cls(pid=pid, process_handle=handle, ee_host_address=ee_host_address, image_path=image_path)
        except Exception:
            kernel32.CloseHandle(handle)
            raise

    def close(self) -> None:
        if not self.process_handle or os.name != "nt":
            return
        import ctypes
        from ctypes import wintypes
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel32.CloseHandle.restype = wintypes.BOOL
        kernel32.CloseHandle(self.process_handle)
        self.process_handle = None

    def read(self, address: int, length: int) -> bytes:
        if address < 0 or length < 0 or address + length > EE_RAM_BYTES:
            raise OracleError(f"Direct EE-RAM read is outside 32 MiB: {format_address(address)} + 0x{length:x}")
        import ctypes
        from ctypes import wintypes
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        kernel32.ReadProcessMemory.restype = wintypes.BOOL
        out = bytearray(length)
        chunk_size = 4 * 1024 * 1024
        for offset in range(0, length, chunk_size):
            size = min(chunk_size, length - offset)
            buf = (ctypes.c_ubyte * size).from_buffer(out, offset)
            read = ctypes.c_size_t()
            if not kernel32.ReadProcessMemory(
                self.process_handle,
                ctypes.c_void_p(self.ee_host_address + address + offset),
                ctypes.cast(buf, ctypes.c_void_p),
                size,
                ctypes.byref(read),
            ) or read.value != size:
                raise OracleError(
                    f"Direct PCSX2 RAM read failed at guest {format_address(address + offset)} "
                    f"(Win32 {ctypes.get_last_error()}, read={read.value}, expected={size})."
                )
        return bytes(out)


def create_fast_ram_reader(pid: int | None, debug: OracleDebugClient) -> WindowsDirectEERamReader | None:
    if os.name != "nt" or not pid:
        return None
    try:
        reader = WindowsDirectEERamReader.open(pid)
        # Counterfactual guard: trust direct host memory only when it byte-matches
        # DebugServer at the already-proved live Level-0 overlay probe.
        length = len(LEVEL0_OVERLAY_PROBE_BYTES)
        direct = reader.read(LEVEL0_OVERLAY_PROBE_PC, length)
        server = bytes.fromhex(debug.read_memory("ee", LEVEL0_OVERLAY_PROBE_PC, length))
        if direct != server:
            raise OracleError("Direct PCSX2 EEmem probe does not match DebugServer memory.")
        print(
            f"Fast EE-RAM backend: direct read-only PCSX2 process memory "
            f"(pid={pid}, EEmem=0x{reader.ee_host_address:x})."
        )
        return reader
    except Exception as exc:
        print(f"Fast EE-RAM backend unavailable ({exc}); falling back to DebugServer reads.")
        try:
            reader.close()  # type: ignore[name-defined]
        except Exception:
            pass
        return None

def read_full_ee_ram(
    client: OracleDebugClient,
    *,
    progress_prefix: str = "RAM",
    fast_reader: WindowsDirectEERamReader | None = None,
) -> bytes:
    if fast_reader is not None:
        started = time.monotonic()
        raw = fast_reader.read(0, EE_RAM_BYTES)
        print(f"  [{progress_prefix}] 32 MiB direct snapshot complete in {time.monotonic() - started:.3f}s")
        return raw

    chunks = bytearray(EE_RAM_BYTES)
    chunk_count = EE_RAM_BYTES // READ_CHUNK_BYTES
    started = time.monotonic()
    for index, address in enumerate(range(0, EE_RAM_BYTES, READ_CHUNK_BYTES), start=1):
        raw = bytes.fromhex(client.read_memory("ee", address, READ_CHUNK_BYTES))
        if len(raw) != READ_CHUNK_BYTES:
            raise OracleError(
                f"DebugServer returned {len(raw)} bytes for {format_address(address)}, "
                f"expected {READ_CHUNK_BYTES}."
            )
        chunks[address:address + READ_CHUNK_BYTES] = raw
        if index == 1 or index % 32 == 0 or index == chunk_count:
            elapsed = time.monotonic() - started
            print(f"  [{progress_prefix}] {index:3d}/{chunk_count} chunks ({index / chunk_count:6.1%}) elapsed={elapsed:5.1f}s")
    return bytes(chunks)


def encode_page_delta(base: bytes, current: bytes) -> tuple[bytes, dict[str, Any]]:
    if len(base) != len(current):
        raise OracleError("Cannot delta RAM snapshots of different lengths.")
    changed_pages: list[tuple[int, bytes]] = []
    changed_bytes = 0
    changed_words = 0
    for page_index, offset in enumerate(range(0, len(base), PAGE_BYTES)):
        before = base[offset:offset + PAGE_BYTES]
        after = current[offset:offset + PAGE_BYTES]
        if before == after:
            continue
        changed_pages.append((page_index, after))
        changed_bytes += sum(a != b for a, b in zip(before, after))
        for word_offset in range(0, len(before), 4):
            if before[word_offset:word_offset + 4] != after[word_offset:word_offset + 4]:
                changed_words += 1
    out = bytearray()
    out += b"ORDELTA1"
    out += struct.pack("<III", PAGE_BYTES, len(base), len(changed_pages))
    for page_index, page in changed_pages:
        out += struct.pack("<I", page_index)
        out += page
    return bytes(out), {
        "format": "ORDELTA1",
        "base_bytes": len(base),
        "page_bytes": PAGE_BYTES,
        "changed_pages": len(changed_pages),
        "changed_bytes": changed_bytes,
        "changed_u32_words": changed_words,
        "delta_sha256": sha256_bytes(bytes(out)),
    }


def decode_page_delta(base: bytes, delta: bytes) -> bytes:
    if not delta.startswith(b"ORDELTA1") or len(delta) < 20:
        raise OracleError("Invalid ORDELTA1 payload.")
    page_bytes, base_bytes, count = struct.unpack_from("<III", delta, 8)
    if page_bytes != PAGE_BYTES or base_bytes != len(base):
        raise OracleError("ORDELTA1 geometry does not match baseline RAM.")
    out = bytearray(base)
    cursor = 20
    for _ in range(count):
        if cursor + 4 + page_bytes > len(delta):
            raise OracleError("Truncated ORDELTA1 payload.")
        page_index = struct.unpack_from("<I", delta, cursor)[0]
        cursor += 4
        offset = page_index * page_bytes
        if offset + page_bytes > len(out):
            raise OracleError("ORDELTA1 page index is out of range.")
        out[offset:offset + page_bytes] = delta[cursor:cursor + page_bytes]
        cursor += page_bytes
    if cursor != len(delta):
        raise OracleError("ORDELTA1 payload has trailing bytes.")
    return bytes(out)




def level0_generation_probe(client: OracleDebugClient) -> dict[str, Any]:
    """Identify the code generation actually resident at the Level-0 probe PC."""
    live = bytes.fromhex(
        client.read_memory("ee", LEVEL0_OVERLAY_PROBE_PC, len(LEVEL0_OVERLAY_PROBE_BYTES))
    )
    if live == LEVEL0_OVERLAY_PROBE_BYTES:
        generation = "level0-overlay"
    elif live == BOOT_ELF_PROBE_BYTES:
        generation = "boot-elf"
    else:
        generation = "unknown"
    return {
        "pc": format_address(LEVEL0_OVERLAY_PROBE_PC),
        "generation": generation,
        "live_hex": live.hex(),
        "level0_overlay_hex": LEVEL0_OVERLAY_PROBE_BYTES.hex(),
        "boot_elf_hex": BOOT_ELF_PROBE_BYTES.hex(),
        "live_sha256": sha256_bytes(live),
    }


def require_level0_overlay_resident(client: OracleDebugClient) -> dict[str, Any]:
    probe = level0_generation_probe(client)
    generation = probe["generation"]
    if generation == "level0-overlay":
        return probe
    if generation == "boot-elf":
        raise OracleError(
            "PCSX2 is still on the boot-ELF code generation at the Level-0 probe. "
            "Wait until normal Veldin gameplay is fully running and Ratchet is controllable, then retry."
        )
    raise OracleError(
        "The live Level-0 code fingerprint matches neither OpenRatchet's extracted Level-0 overlay "
        "nor its boot ELF. This is the only case that indicates a different executable/disc build. "
        f"Live bytes at {format_address(LEVEL0_OVERLAY_PROBE_PC)}: {probe['live_hex']}"
    )


def assert_no_unrelated_debug_stops(client: OracleDebugClient) -> None:
    foreign_bps = [
        bp for bp in client.list_breakpoints("ee")
        if not str(bp.get("description", "")).startswith("OpenRatchet oracle:")
    ]
    foreign_mcs = [
        mc for mc in client.list_memchecks("ee")
        if not str(mc.get("description", "")).startswith("OpenRatchet oracle:")
    ]
    if foreign_bps or foreign_mcs:
        raise OracleError(
            "PCSX2 has unrelated debugger breakpoints/watchpoints armed. Remove them before capture "
            f"(breakpoints={len(foreign_bps)}, watchpoints={len(foreign_mcs)})."
        )


def force_pause(client: OracleDebugClient, *, timeout: float = 5.0) -> dict[str, Any]:
    status = client.status("ee")
    if bool(status.get("paused", False)):
        return status
    client.pause("ee")
    status = wait_until_paused(client, timeout=timeout)
    if not bool(status.get("paused", False)):
        raise OracleError("PCSX2 did not enter a paused state when requested.")
    return status


def status_cycle_value(status: dict[str, Any]) -> int | None:
    raw = status.get("cycles")
    if raw is None:
        return None
    try:
        return parse_address(raw, "status.cycles")
    except CaptureError:
        try:
            return int(raw)
        except (TypeError, ValueError):
            return None


def capture_generic_checkpoint(
    client: OracleDebugClient,
    name: str,
    status: dict[str, Any] | None = None,
) -> dict[str, Any]:
    status = status or client.status("ee")
    pc = parse_address(status.get("pc", 0), "status.pc")
    disasm_start = max(0, pc - 16)
    return {
        "name": name,
        "pc": format_address(pc),
        "cycles": status.get("cycles"),
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "registers": client.request("read_registers", cpu="ee", category=-1).get("data", {}),
        "backtrace": client.backtrace("ee", 32),
        "disassembly": client.disassemble("ee", disasm_start, 12),
        "code_generation": level0_generation_probe(client),
    }


def run_timed_retail_window(
    client: OracleDebugClient,
    *,
    run_ms: int,
) -> tuple[dict[str, Any], dict[str, Any]]:
    if run_ms < 50:
        raise OracleError("Retail scenario window must be at least 50 ms.")
    before = force_pause(client)
    before_cycles = status_cycle_value(before)
    started = time.monotonic()
    client.resume("ee")
    time.sleep(run_ms / 1000.0)
    after = force_pause(client)
    elapsed_ms = (time.monotonic() - started) * 1000.0
    after_cycles = status_cycle_value(after)
    info: dict[str, Any] = {
        "requested_host_ms": run_ms,
        "actual_host_ms": round(elapsed_ms, 3),
        "cycles_before": before.get("cycles"),
        "cycles_after": after.get("cycles"),
    }
    if before_cycles is not None and after_cycles is not None:
        info["cycle_delta"] = after_cycles - before_cycles
    return after, info


def changed_page_stats(before: bytes, after: bytes) -> list[dict[str, int]]:
    if len(before) != len(after):
        raise OracleError("Cannot compare RAM snapshots of different lengths.")
    stats: list[dict[str, int]] = []
    for page_index, offset in enumerate(range(0, len(before), PAGE_BYTES)):
        a = before[offset:offset + PAGE_BYTES]
        b = after[offset:offset + PAGE_BYTES]
        if a == b:
            continue
        changed_offsets = [i for i, (x, y) in enumerate(zip(a, b)) if x != y]
        changed_words = sum(
            a[i:i + 4] != b[i:i + 4] for i in range(0, min(len(a), len(b)), 4)
        )
        stats.append({
            "page_index": page_index,
            "address": offset,
            "changed_bytes": len(changed_offsets),
            "changed_u32_words": changed_words,
            "first_changed_offset": changed_offsets[0],
            "last_changed_offset": changed_offsets[-1],
        })
    return stats


def rank_writer_pages(
    target_name: str,
    idle_ram: bytes,
    scenario_rams: dict[str, bytes],
    family_names: Iterable[str],
    *,
    limit: int,
) -> list[dict[str, int]]:
    if target_name not in scenario_rams:
        return []
    family_sets: dict[str, set[int]] = {}
    target_stats = changed_page_stats(idle_ram, scenario_rams[target_name])
    target_by_page = {item["page_index"]: item for item in target_stats}
    for name in family_names:
        ram = scenario_rams.get(name)
        if ram is None:
            continue
        family_sets[name] = {item["page_index"] for item in changed_page_stats(idle_ram, ram)}
    ranked: list[dict[str, int]] = []
    for page_index, item in target_by_page.items():
        frequency = sum(page_index in pages for pages in family_sets.values())
        ranked.append({**item, "family_frequency": frequency})
    ranked.sort(
        key=lambda item: (
            item["family_frequency"],
            item["changed_bytes"],
            item["changed_u32_words"],
        ),
        reverse=True,
    )
    return ranked[:limit]


def arm_page_memchecks(client: OracleDebugClient, pages: list[dict[str, int]]) -> None:
    remove_owned_memchecks(client)
    for item in pages:
        start = item["address"]
        end = min(EE_RAM_BYTES, start + PAGE_BYTES)
        client.set_memcheck(
            "ee",
            start,
            end,
            f"OpenRatchet oracle:diff-page-{item['page_index']:04x}",
            kind="write",
        )


def memcheck_hit_count(mc: dict[str, Any]) -> int:
    try:
        return int(mc.get("hits", 0))
    except (TypeError, ValueError):
        return 0


def capture_first_page_writers(
    client: OracleDebugClient,
    pages: list[dict[str, int]],
    *,
    timeout: float,
    max_events: int,
) -> tuple[list[dict[str, Any]], list[int]]:
    """Capture first Retail writers for ranked pages without trusting stop-PC equality.

    PCSX2 memchecks report their own `hits`, `last_pc` and `last_addr`. The EE stop PC
    can legitimately differ from `last_pc` (for example around branch/delay-slot or JIT
    boundaries), so an exact stop-PC match is not a sound hit oracle.
    """
    if not pages:
        return [], []
    arm_page_memchecks(client, pages)
    pending = {item["page_index"] for item in pages}
    events: list[dict[str, Any]] = []
    baseline_hits: dict[int, int] = {}
    for mc in client.list_memchecks("ee"):
        desc = str(mc.get("description", ""))
        match = re.search(r"diff-page-([0-9a-fA-F]+)$", desc)
        if match:
            baseline_hits[int(match.group(1), 16)] = memcheck_hit_count(mc)

    started = time.monotonic()
    unmatched_pauses = 0
    client.resume("ee")
    while pending and len(events) < max_events:
        remaining = timeout - (time.monotonic() - started)
        if remaining <= 0:
            break
        try:
            status = wait_until_paused(client, timeout=min(remaining, 1.0))
        except OracleError:
            force_pause(client)
            break

        stop_pc = parse_address(status.get("pc", 0), "status.pc")
        memchecks = client.list_memchecks("ee")
        hit_any = False
        for item in memchecks:
            desc = str(item.get("description", ""))
            match = re.search(r"diff-page-([0-9a-fA-F]+)$", desc)
            if not match:
                continue
            page_index = int(match.group(1), 16)
            if page_index not in pending:
                continue
            hits = memcheck_hit_count(item)
            if hits <= baseline_hits.get(page_index, 0):
                continue
            hit_any = True
            writer_pc = parse_address(item.get("last_pc", 0), "memcheck.last_pc")
            last_addr = parse_address(item.get("last_addr", 0), "memcheck.last_addr")
            events.append({
                "ordinal": len(events) + 1,
                "page_index": page_index,
                "page_address": format_address(page_index * PAGE_BYTES),
                "stop_pc": format_address(stop_pc),
                "pc": format_address(writer_pc),
                "cycles": status.get("cycles"),
                "address": format_address(last_addr),
                "gpr": client.read_registers("ee", 0),
                "backtrace": client.backtrace("ee", 24),
                "disassembly": client.disassemble("ee", max(0, writer_pc - 16), 12),
            })
            pending.remove(page_index)
            start = page_index * PAGE_BYTES
            client.remove_memcheck("ee", start, min(EE_RAM_BYTES, start + PAGE_BYTES))

        if not hit_any:
            unmatched_pauses += 1
            # Do not throw away a complete oracle because PCSX2 paused for a debugger/JIT
            # reason unrelated to our owned watchpoints. Resume and let the finite timeout
            # decide whether writer evidence is available.
            if unmatched_pauses <= 3:
                print(f"  Writer trace note: non-owned pause at {format_address(stop_pc)}; resuming.")
            elif unmatched_pauses == 4:
                print("  Writer trace note: suppressing further non-owned pause messages.")

        if pending and len(events) < max_events:
            client.resume("ee")

    force_pause(client)
    remove_owned_memchecks(client)
    return events, sorted(pending)

def breakpoint_desc(name: str) -> str:
    return f"OpenRatchet oracle:{name}"


def ensure_breakpoint(client: OracleDebugClient, address: int, name: str) -> None:
    existing = {parse_address(bp.get("address", 0), "breakpoint.address") for bp in client.list_breakpoints("ee")}
    if address in existing:
        return
    client.request(
        "set_breakpoint",
        cpu="ee",
        address=format_address(address),
        condition="",
        description=breakpoint_desc(name),
        temporary=False,
        enabled=True,
    )


def remove_owned_breakpoints(client: OracleDebugClient) -> None:
    for bp in client.list_breakpoints("ee"):
        desc = str(bp.get("description", ""))
        if not desc.startswith("OpenRatchet oracle:"):
            continue
        client.remove_breakpoint("ee", parse_address(bp.get("address", 0), "breakpoint.address"))


def remove_owned_memchecks(client: OracleDebugClient) -> None:
    for mc in client.list_memchecks("ee"):
        desc = str(mc.get("description", ""))
        if not desc.startswith("OpenRatchet oracle:"):
            continue
        start = parse_address(mc.get("start", 0), "memcheck.start")
        end = parse_address(mc.get("end", start + 4), "memcheck.end")
        client.remove_memcheck("ee", start, end)


def wait_until_paused(
    client: OracleDebugClient,
    *,
    timeout: float,
    expected_pcs: Iterable[int] | None = None,
) -> dict[str, Any]:
    expected = set(expected_pcs or [])
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = client.status("ee")
        if not bool(status.get("alive", True)):
            raise OracleError("PCSX2 VM stopped during capture.")
        if bool(status.get("paused", False)):
            pc = parse_address(status.get("pc", 0), "status.pc")
            if not expected or pc in expected:
                return status
            return status
        time.sleep(0.02)
    raise OracleError(f"Timed out after {timeout:g}s waiting for PCSX2 to pause.")


def reach_frame_pre(client: OracleDebugClient, *, timeout: float = 30.0) -> dict[str, Any]:
    ensure_breakpoint(client, FRAME_PRE_PC, "frame-pre")
    status = client.status("ee")
    if bool(status.get("paused", False)) and parse_address(status.get("pc", 0), "status.pc") == FRAME_PRE_PC:
        return status
    if bool(status.get("paused", False)):
        client.resume("ee")
    status = wait_until_paused(client, timeout=timeout)
    pc = parse_address(status.get("pc", 0), "status.pc")
    if pc != FRAME_PRE_PC:
        raise OracleError(
            f"PCSX2 paused at {format_address(pc)} instead of Level-0 frame-pre "
            f"{format_address(FRAME_PRE_PC)}. Remove unrelated breakpoints or reach initial Veldin gameplay."
        )
    return status


def read_live_u32(client: OracleDebugClient, address: int) -> int:
    raw = bytes.fromhex(client.read_memory("ee", address, 4))
    if len(raw) != 4:
        raise OracleError(
            f"PCSX2 returned {len(raw)} bytes while reading {format_address(address)}; expected 4."
        )
    return int.from_bytes(raw, "little")


def verify_frame_pre_signature(client: OracleDebugClient) -> None:
    raw = bytes.fromhex(client.read_memory("ee", FRAME_PRE_PC, 4))
    if len(raw) != 4:
        raise OracleError("Could not read the Retail frame-pre instruction signature.")
    actual = int.from_bytes(raw, "little")
    if actual == FRAME_PRE_OPCODE:
        return
    disasm = client.disassemble("ee", FRAME_PRE_PC, 1)
    text = str(disasm[0].get("disasm", "(unavailable)")) if disasm else "(unavailable)"
    raise OracleError(
        "Retail checkpoint signature mismatch. "
        f"At {format_address(FRAME_PRE_PC)} PCSX2 has opcode {format_address(actual)} "
        f"({text}), but OpenRatchet's authoritative PS2_MAIN.ELF expects "
        f"{format_address(FRAME_PRE_OPCODE)} (jal FUN_00217A10). "
        "This means the running Retail executable/code generation does not match the "
        "OpenRatchet address map; do not capture from this state/build."
    )


def probe_live_moby_pool(client: OracleDebugClient) -> tuple[bool, str, dict[str, int]]:
    base = read_live_u32(client, MOBY_POOL_PTR)
    last = read_live_u32(client, MOBY_LAST_PTR)
    details = {"base": base, "last": last, "ratchet": 0}
    if base == 0 or base > EE_RAM_BYTES - MOBY_POOL_BYTES or (base & 0xFF) != 0:
        return False, f"pool base not published ({format_address(base)})", details
    expected_last = base + 0x3F00
    if last != expected_last:
        return (
            False,
            f"pool last mismatch ({format_address(last)} != {format_address(expected_last)})",
            details,
        )
    pool = bytes.fromhex(client.read_memory("ee", base, MOBY_POOL_BYTES))
    if len(pool) != MOBY_POOL_BYTES:
        return False, f"pool read truncated ({len(pool)} bytes)", details
    ratchets: list[int] = []
    terminator_found = False
    for slot in range(64):
        offset = slot * MOBY_RECORD_BYTES
        traversal = signed8(pool[offset + MOBY_TRAVERSAL_OFFSET])
        if traversal == -1:
            terminator_found = True
            break
        if traversal < 0:
            continue
        if le_u16(pool, offset + MOBY_OCLASS_OFFSET) == 0:
            ratchets.append(base + offset)
    if not terminator_found:
        return False, "Retail Moby traversal terminator not published yet", details
    if len(ratchets) != 1:
        return False, f"expected one live oClass=0 Ratchet, found {len(ratchets)}", details
    details["ratchet"] = ratchets[0]
    return True, "ok", details


def reach_gameplay_frame_pre(
    client: OracleDebugClient,
    *,
    timeout: float = 30.0,
) -> tuple[dict[str, Any], dict[str, int]]:
    """Stop only at a frame-pre where the proved Retail gameplay pool is live.

    FRAME_PRE_PC occurs before the gameplay state is necessarily ready.  The PC alone
    is evidence, not the phase gate.  We therefore fail fast on an executable-signature
    mismatch and otherwise resume across early hits until the authentic Retail Moby
    pool and unique live Ratchet are published.
    """
    deadline = time.monotonic() + timeout
    hits = 0
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise OracleError(
                f"Timed out after {timeout:g}s waiting for a gameplay-ready frame-pre. "
                "The frame PC was reachable, but the authentic Retail Moby pool never became ready."
            )
        status = reach_frame_pre(client, timeout=remaining)
        verify_frame_pre_signature(client)
        ready, reason, details = probe_live_moby_pool(client)
        hits += 1
        if ready:
            print(
                "Gameplay-ready Retail gate: "
                f"pool={format_address(details['base'])} "
                f"last={format_address(details['last'])} "
                f"Ratchet={format_address(details['ratchet'])} "
                f"frame-pre-hits={hits}"
            )
            return status, details
        if hits <= 3 or hits % 30 == 0:
            print(f"  Waiting for gameplay-ready Retail state (frame-pre hit {hits}: {reason})...")
        client.resume("ee")


def capture_checkpoint(client: OracleDebugClient, name: str, status: dict[str, Any], semantic_ram: bytes | None = None) -> dict[str, Any]:
    pc = parse_address(status.get("pc", 0), "status.pc")
    data = {
        "name": name,
        "pc": format_address(pc),
        "cycles": status.get("cycles"),
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "registers": client.request("read_registers", cpu="ee", category=-1).get("data", {}),
        "backtrace": client.backtrace("ee", 32),
        "controller_hex": client.read_memory("ee", CONTROLLER_STATE, CONTROLLER_BYTES),
        "camera_hex": client.read_memory("ee", CAMERA_STATE, CAMERA_BYTES),
    }
    if semantic_ram is not None:
        data["semantic"] = semantic_from_ram(semantic_ram).as_json()
    return data


def memcheck_matches_pc(mc: dict[str, Any], pc: int) -> bool:
    try:
        return parse_address(mc.get("last_pc", 0), "memcheck.last_pc") == pc
    except CaptureError:
        return False


def capture_one_frame(
    client: OracleDebugClient,
    baseline_semantic: SemanticState,
    *,
    max_events: int,
    timeout: float,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    ensure_breakpoint(client, FRAME_POST_INPUT_PC, "post-input")
    ensure_breakpoint(client, FRAME_END_PC, "frame-end")
    remove_owned_memchecks(client)
    watch_ranges = build_watch_ranges(baseline_semantic)
    for item in watch_ranges:
        client.set_memcheck(
            "ee", item.start, item.end, f"OpenRatchet oracle:{item.name}", kind="write"
        )

    checkpoints: list[dict[str, Any]] = []
    events: list[dict[str, Any]] = []
    seen_disasm: dict[int, list[dict[str, Any]]] = {}
    started = time.monotonic()
    client.resume("ee")

    while True:
        remaining = timeout - (time.monotonic() - started)
        if remaining <= 0:
            raise OracleError("Timed out while advancing the captured Retail frame.")
        status = wait_until_paused(client, timeout=min(remaining, 5.0))
        pc = parse_address(status.get("pc", 0), "status.pc")
        if pc == FRAME_POST_INPUT_PC:
            checkpoints.append(capture_checkpoint(client, "post-input", status))
            client.resume("ee")
            continue
        if pc == FRAME_END_PC:
            checkpoints.append(capture_checkpoint(client, "frame-end", status))
            break

        memchecks = client.list_memchecks("ee")
        hit = [item for item in memchecks if memcheck_matches_pc(item, pc)]
        if not hit:
            raise OracleError(
                f"Unexpected PCSX2 pause at {format_address(pc)} while capturing frame. "
                "A user breakpoint or debugger condition interrupted the oracle run."
            )
        for item in hit:
            if len(events) >= max_events:
                raise OracleError(
                    f"Writer event limit ({max_events}) exceeded in one frame. "
                    "This is intentionally fail-closed; narrow the watched domains before retrying."
                )
            last_addr = parse_address(item.get("last_addr", 0), "memcheck.last_addr")
            if pc not in seen_disasm:
                start = max(0, pc - 16)
                seen_disasm[pc] = client.disassemble("ee", start, 12)
            window_start = max(0, last_addr - 16)
            window_len = min(64, EE_RAM_BYTES - window_start)
            events.append({
                "ordinal": len(events) + 1,
                "pc": format_address(pc),
                "cycles": status.get("cycles"),
                "range": item.get("description", ""),
                "address": format_address(last_addr),
                "hit_count": item.get("hits"),
                "gpr": client.read_registers("ee", 0),
                "backtrace": client.backtrace("ee", 24),
                "memory_window": {
                    "address": format_address(window_start),
                    "hex": client.read_memory("ee", window_start, window_len),
                },
                "disassembly": seen_disasm[pc],
            })
        client.resume("ee")

    remove_owned_memchecks(client)
    return checkpoints, events


def _beep(frequency: int, duration_ms: int, *, enabled: bool) -> None:
    if not enabled:
        return
    if sys.platform == "win32":
        try:
            import winsound
            winsound.Beep(frequency, duration_ms)
            return
        except (ImportError, RuntimeError, OSError):
            pass
    print("\a", end="", flush=True)


def _speak(text: str, *, enabled: bool) -> None:
    """Best-effort Windows speech without adding a Python dependency."""
    if not enabled or sys.platform != "win32":
        return
    escaped = text.replace("'", "''")
    script = (
        "Add-Type -AssemblyName System.Speech; "
        "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
        "$voices = @($s.GetInstalledVoices() | ForEach-Object { $_.VoiceInfo }); "
        "$v = $voices | Where-Object { $_.Culture.Name -eq 'en-US' } | Select-Object -First 1; "
        "if (-not $v) { $v = $voices | Where-Object { $_.Culture.Name -eq 'en-GB' } | Select-Object -First 1 }; "
        "if (-not $v) { $v = $voices | Where-Object { $_.Culture.TwoLetterISOLanguageName -eq 'en' } | Select-Object -First 1 }; "
        "if ($v) { $s.SelectVoice($v.Name) } else { exit 3 }; "
        "$s.Rate = 1; "
        f"$s.Speak('{escaped}')"
    )
    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    try:
        subprocess.run(
            ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=12,
            creationflags=creationflags,
        )
    except (OSError, subprocess.SubprocessError):
        # Speech is ergonomic only; capture correctness must never depend on it.
        pass


def automatic_arm_countdown(seconds: int, *, speech: bool, beep: bool) -> None:
    print()
    print("No more terminal input is required after this point.")
    print("Focus PCSX2 now and release every controller input.")
    _speak(
        f"Retail oracle capture starts in {seconds} seconds. Focus PCSX2 and release all controls.",
        enabled=speech,
    )
    for remaining in range(seconds, 0, -1):
        print(f"Arming in {remaining}...", flush=True)
        _beep(550 if remaining > 3 else 700, 120, enabled=beep)
        time.sleep(0.88)
    _beep(1000, 350, enabled=beep)
    print("ARMED", flush=True)


def automatic_scenario_countdown(
    name: str,
    instruction: str,
    seconds: int,
    *,
    speech: bool,
    beep: bool,
) -> None:
    print()
    print(f"=== Scenario: {name} ===")
    print(instruction)
    if name == "idle":
        print("Keep ALL controls released through CAPTURE.")
        spoken = f"Scenario idle. {instruction} Keep all controls released until the done sound."
    else:
        print("At the DOUBLE BEEP (2), start holding the requested input.")
        print("Keep it held through CAPTURE until the DONE sound.")
        spoken = (
            f"Scenario {name.replace('_', ' ')}. {instruction} "
            "Start holding at the double beep. Keep holding until the done sound."
        )
    _speak(spoken, enabled=speech)
    time.sleep(0.5)

    for remaining in range(seconds, 0, -1):
        if remaining == 2:
            cue = "KEEP RELEASED" if name == "idle" else "HOLD NOW"
            print(f"{remaining}  <<< {cue} >>>", flush=True)
            _beep(850, 120, enabled=beep)
            time.sleep(0.10)
            _beep(850, 120, enabled=beep)
            time.sleep(0.66)
        else:
            print(str(remaining), flush=True)
            _beep(650 + min(remaining, 3) * 80, 120, enabled=beep)
            time.sleep(0.88)

    print("CAPTURE", flush=True)
    _beep(1150, 350, enabled=beep)


def automatic_capture_done(*, speech: bool, beep: bool) -> None:
    # The EE is paused at the end of the fixed Retail input window when this is
    # called, so releasing now cannot change the snapshot copied afterward.
    _beep(950, 140, enabled=beep)
    time.sleep(0.08)
    _beep(650, 260, enabled=beep)
    print("DONE - release the controller input now.", flush=True)
    _speak("Done. Release the controller input now.", enabled=speech)


def scenario_defs(path: Path | None) -> list[dict[str, str]]:
    if path is None:
        return [{"name": name, "instruction": instruction} for name, instruction in DEFAULT_SCENARIOS]
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict) or raw.get("schema") != 1:
        raise OracleError("Scenario manifest requires schema: 1.")
    items = raw.get("scenarios")
    if not isinstance(items, list) or not items:
        raise OracleError("Scenario manifest requires a non-empty scenarios array.")
    out: list[dict[str, str]] = []
    names: set[str] = set()
    for index, item in enumerate(items):
        if not isinstance(item, dict):
            raise OracleError(f"scenarios[{index}] must be an object.")
        name = str(item.get("name", "")).strip()
        instruction = str(item.get("instruction", "")).strip()
        if not name or not re.fullmatch(r"[A-Za-z0-9_.-]+", name):
            raise OracleError(f"Invalid scenario name at index {index}: {name!r}")
        if name in names:
            raise OracleError(f"Duplicate scenario name: {name}")
        if not instruction:
            raise OracleError(f"Scenario {name!r} requires an instruction.")
        names.add(name)
        out.append({"name": name, "instruction": instruction})
    return out


def write_oracle_readme(path: Path) -> None:
    path.write_text(
        """# OpenRatchet R&C1 Retail Oracle — Level 0

This archive is **investigation evidence only**. PCSX2 is a differential/reference oracle;
nothing in this archive is a shipping OpenRatchet runtime dependency.

The capture first proves that the live Retail Level-0 overlay generation is resident. It does
**not** use stale boot-ELF frame addresses or boot-generation gameplay globals as capture gates.
Every input scenario restores the exact same savestate and runs a fixed host-time Retail window.

## Layout

- `manifest.json`: game/PCSX2 identity, capture alignment, code-generation fingerprint and hashes.
- `baseline/ee_ram.bin`: complete 32 MiB EE RDRAM from the common paused Veldin baseline.
- `baseline/checkpoint.json`: baseline PC/register/backtrace/disassembly evidence.
- `baseline/code_generation.json`: exact Level-0 overlay fingerprint observed in live Retail memory.
- `scenarios/<name>/checkpoints.json`: CPU/register/backtrace/disassembly before and after the input window.
- `scenarios/<name>/timing.json`: requested/actual host window and EE cycle delta when available.
- `scenarios/<name>/ee_ram.delta`: sparse `ORDELTA1` page delta against the baseline RAM.
- `scenarios/<name>/vs_idle.delta`: input-vs-idle RAM delta for non-idle scenarios.
- `scenarios/<name>/vs_idle_pages.json`: exact changed-page/byte counts for that input differential.
- `scenarios/move_forward/writers.json` and `scenarios/camera_right/writers.json`: first Retail
  writer PCs for the highest-ranked input-specific pages, including GPR/backtrace/disassembly.

## Why the code-generation fingerprint matters

Retail Veldin replaces the boot-ELF code in the Level-0 overlay address range. Address equality
alone is therefore not an execution oracle. This capture deliberately accepts the extracted
Level-0 overlay fingerprint as the expected controllable-Veldin generation and rejects only an
unknown fingerprint.

## ORDELTA1

Binary little-endian format: `magic[8]='ORDELTA1'`, `u32 pageSize`, `u32 baseSize`,
`u32 changedPageCount`, followed by `changedPageCount` records of `u32 pageIndex` plus
exactly `pageSize` replacement bytes. Applying each replacement page to the baseline
reconstructs the full scenario snapshot byte-for-byte.
""",
        encoding="utf-8",
    )


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def add_hash_manifest(root: Path, manifest: dict[str, Any]) -> None:
    hashes: dict[str, dict[str, Any]] = {}
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.name == "manifest.json":
            continue
        rel = path.relative_to(root).as_posix()
        hashes[rel] = {"bytes": path.stat().st_size, "sha256": file_sha256(path)}
    manifest["files"] = hashes


def create_zip(root: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    temp = output.with_suffix(output.suffix + ".tmp")
    if temp.exists():
        temp.unlink()
    with zipfile.ZipFile(temp, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(root.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(root).as_posix())
    temp.replace(output)


def default_output() -> Path:
    stamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    return Path("build") / "reference-captures" / f"RAC1_RETAIL_ORACLE_LEVEL0_{stamp}.zip"


def capture(args: argparse.Namespace) -> int:
    scenarios = scenario_defs(args.scenarios)
    # Preserve the authored controller-friendly order; only move idle to the front.
    idle = [item for item in scenarios if item["name"] == "idle"]
    rest = [item for item in scenarios if item["name"] != "idle"]
    if len(idle) != 1:
        raise OracleError("Retail oracle scenario set must contain exactly one 'idle' reference scenario.")
    scenarios = idle + rest

    output = args.output or default_output()
    debug = OracleDebugClient(
        args.host,
        args.debug_port,
        minimum_request_interval=args.request_interval_ms / 1000.0,
    )
    pine = PineClient(args.host, args.pine_port)

    temp_root = Path(tempfile.mkdtemp(prefix="openratchet-retail-oracle-"))
    scenario_rams: dict[str, bytes] = {}
    manifest_scenarios: dict[str, dict[str, Any]] = {}
    fast_reader: WindowsDirectEERamReader | None = None
    try:
        print("Connecting to PCSX2 DebugServer and Pine IPC...")
        status = debug.status("ee")
        if not bool(status.get("alive", True)):
            raise OracleError("PCSX2 DebugServer is reachable but the EE VM is not alive.")
        pine.connect()
        game = {
            "title": pine.title(),
            "id": pine.game_id(),
            "uuid": pine.uuid(),
            "game_version": pine.game_version(),
            "pcsx2_version": pine.version(),
        }
        print(f"PCSX2: {game['pcsx2_version'] or '(unknown version)'}")
        print(f"Game:  {game['title'] or '(unknown title)'} | {game['id'] or '(unknown id)'}")
        if game["game_version"]:
            print(f"Game version: {game['game_version']}")
        print()
        print("Stand on controllable Veldin gameplay. This capture intentionally does NOT use")
        print("boot-ELF frame addresses: Retail Level 0 replaces that code with its overlay.")
        automatic_arm_countdown(
            args.arm_countdown,
            speech=not args.no_speech,
            beep=not args.no_beep,
        )

        remove_owned_memchecks(debug)
        remove_owned_breakpoints(debug)
        assert_no_unrelated_debug_stops(debug)
        baseline_status = force_pause(debug)
        generation = require_level0_overlay_resident(debug)
        live_opcode = int.from_bytes(bytes.fromhex(generation["live_hex"])[:4], "little")
        print(
            "Retail Level-0 overlay generation CONFIRMED: "
            f"probe={generation['pc']} opcode={format_address(live_opcode)}."
        )
        print("This is the expected controllable-Veldin generation; the old boot-ELF checkpoint is obsolete.")
        fast_reader = create_fast_ram_reader(args.pcsx2_pid, debug)
        if args.require_fast_ram and fast_reader is None:
            raise OracleError(
                "Fast direct PCSX2 EE-RAM capture could not be established. Refusing to fall back "
                "to the old ~51-second-per-snapshot DebugServer path. Re-run with --allow-slow "
                "only if you explicitly want the slow fallback."
            )

        print(f"Saving reproducible baseline to PCSX2 savestate slot {args.savestate_slot}...")
        pine.save_state(args.savestate_slot)
        time.sleep(args.savestate_settle)
        baseline_status = force_pause(debug)
        require_level0_overlay_resident(debug)

        baseline_checkpoint = capture_generic_checkpoint(debug, "baseline-paused", baseline_status)
        print("Capturing complete 32 MiB baseline EE RAM (one time)...")
        baseline_ram = read_full_ee_ram(debug, progress_prefix="baseline", fast_reader=fast_reader)
        baseline_dir = temp_root / "baseline"
        baseline_dir.mkdir(parents=True, exist_ok=True)
        (baseline_dir / "ee_ram.bin").write_bytes(baseline_ram)
        json_dump(baseline_dir / "checkpoint.json", baseline_checkpoint)
        json_dump(baseline_dir / "code_generation.json", generation)

        manifest: dict[str, Any] = {
            "schema": SCHEMA,
            "kind": "RAC1_RETAIL_ORACLE_LEVEL0",
            "created_utc": datetime.now(timezone.utc).isoformat(),
            "game": game,
            "transport": {
                "debug_server": f"{args.host}:{args.debug_port}",
                "pine": f"{args.host}:{args.pine_port}",
                "request_interval_ms": args.request_interval_ms,
                "savestate_slot": args.savestate_slot,
                "ram_snapshot_backend": "direct-pcsx2-EEmem" if fast_reader is not None else "debug-server-64KiB",
                "pcsx2_pid": args.pcsx2_pid,
            },
            "capture_alignment": {
                "mode": "same-savestate-fixed-host-window",
                "scenario_run_ms": args.scenario_run_ms,
                "reason": "Level-0 Retail gameplay runs overlay code; boot-ELF checkpoint addresses are stale.",
                "code_generation": generation,
                "per_scenario_cycle_delta_recorded": True,
            },
            "baseline": {
                "ee_ram_bytes": len(baseline_ram),
                "ee_ram_sha256": sha256_bytes(baseline_ram),
            },
            "writer_discovery": {
                "targets": ["move_forward", "camera_right"],
                "page_limit": args.writer_page_limit,
                "event_limit": args.writer_event_limit,
                "method": "first-writer-on-ranked-input-specific-pages",
            },
            "scenarios": [],
            "policy": {
                "pcsx2_role": "reference-or-differential-oracle-only",
                "guest_memory_writes": False,
                "host_gameplay_substitution": False,
                "temporary_probe": True,
            },
        }
        write_oracle_readme(temp_root / "README.md")

        idle_post_ram: bytes | None = None
        for index, scenario in enumerate(scenarios, start=1):
            name = scenario["name"]
            print()
            print(f"[{index}/{len(scenarios)}] Restoring baseline for {name}...")
            remove_owned_memchecks(debug)
            remove_owned_breakpoints(debug)
            pine.load_state(args.savestate_slot)
            time.sleep(args.savestate_settle)
            pre_status = force_pause(debug)
            require_level0_overlay_resident(debug)
            pre = capture_generic_checkpoint(debug, "pre-input-window", pre_status)

            automatic_scenario_countdown(
                name,
                scenario["instruction"],
                args.scenario_countdown,
                speech=not args.no_speech,
                beep=not args.no_beep,
            )
            post_status, timing = run_timed_retail_window(
                debug,
                run_ms=args.scenario_run_ms,
            )
            automatic_capture_done(speech=not args.no_speech, beep=not args.no_beep)
            post = capture_generic_checkpoint(debug, "post-input-window", post_status)

            print(
                f"  Retail window complete: requested={timing['requested_host_ms']} ms "
                f"actual={timing['actual_host_ms']} ms"
                + (f" cycles={timing['cycle_delta']}" if "cycle_delta" in timing else "")
            )
            print("  Capturing full post-window EE RAM delta source...")
            current_ram = read_full_ee_ram(debug, progress_prefix=name, fast_reader=fast_reader)
            scenario_rams[name] = current_ram
            delta, delta_summary = encode_page_delta(baseline_ram, current_ram)

            scenario_dir = temp_root / "scenarios" / name
            scenario_dir.mkdir(parents=True, exist_ok=True)
            (scenario_dir / "ee_ram.delta").write_bytes(delta)
            json_dump(scenario_dir / "writers.json", [])
            json_dump(scenario_dir / "checkpoints.json", [pre, post])
            json_dump(scenario_dir / "timing.json", timing)

            vs_idle_summary: dict[str, Any] | None = None
            if name == "idle":
                idle_post_ram = current_ram
            elif idle_post_ram is not None:
                vs_idle_delta, vs_idle_summary = encode_page_delta(idle_post_ram, current_ram)
                (scenario_dir / "vs_idle.delta").write_bytes(vs_idle_delta)
                page_stats = changed_page_stats(idle_post_ram, current_ram)
                json_dump(scenario_dir / "vs_idle_pages.json", page_stats)

            diff = {
                **delta_summary,
                "post_ram_sha256": sha256_bytes(current_ram),
                "writer_events": 0,
                "timing": timing,
                "vs_idle": vs_idle_summary,
            }
            json_dump(scenario_dir / "diff.json", diff)
            scenario_manifest = {
                "name": name,
                "instruction": scenario["instruction"],
                "delta": diff,
            }
            manifest["scenarios"].append(scenario_manifest)
            manifest_scenarios[name] = scenario_manifest

        if idle_post_ram is None:
            raise OracleError("Internal capture error: idle post-window RAM was not captured.")

        # Phase-12 writer evidence is now derived FROM the Retail input differential,
        # not from guessed boot-generation Ratchet/controller globals.
        writer_jobs = (
            ("move_forward", ("move_forward", "move_backward", "move_left", "move_right")),
            ("camera_right", ("camera_left", "camera_right", "camera_up", "camera_down")),
        )
        for target_name, family in writer_jobs:
            if target_name not in scenario_rams:
                continue
            candidates = rank_writer_pages(
                target_name,
                idle_post_ram,
                scenario_rams,
                family,
                limit=args.writer_page_limit,
            )
            scenario_dir = temp_root / "scenarios" / target_name
            json_dump(scenario_dir / "writer_candidate_pages.json", candidates)
            if not candidates:
                print(f"No input-specific pages found for writer trace {target_name}; keeping state-only evidence.")
                continue

            scenario = next(item for item in scenarios if item["name"] == target_name)
            print()
            print(f"=== Writer trace replay: {target_name} ===")
            print(f"Tracing first writers for {len(candidates)} ranked input-specific RAM pages.")
            pine.load_state(args.savestate_slot)
            time.sleep(args.savestate_settle)
            force_pause(debug)
            require_level0_overlay_resident(debug)
            automatic_scenario_countdown(
                target_name,
                "Repeat the same input. " + scenario["instruction"],
                args.scenario_countdown,
                speech=not args.no_speech,
                beep=not args.no_beep,
            )
            writer_error: str | None = None
            try:
                events, missed = capture_first_page_writers(
                    debug,
                    candidates,
                    timeout=args.writer_timeout,
                    max_events=args.writer_event_limit,
                )
            except (OracleError, CaptureError, OSError) as exc:
                # Writer tracing is enrichment. Never discard the expensive, already-complete
                # deterministic RAM oracle because a watchpoint replay was imperfect.
                writer_error = str(exc)
                events, missed = [], [item["page_index"] for item in candidates]
                print(f"  Writer trace unavailable: {writer_error}")
                try:
                    force_pause(debug)
                    remove_owned_memchecks(debug)
                except Exception:
                    pass
            automatic_capture_done(speech=not args.no_speech, beep=not args.no_beep)
            json_dump(scenario_dir / "writers.json", events)
            json_dump(
                scenario_dir / "writer_trace.json",
                {
                    "candidate_pages": candidates,
                    "events": len(events),
                    "missed_page_indices": missed,
                    "error": writer_error,
                    "post_trace_checkpoint": capture_generic_checkpoint(debug, "post-writer-trace"),
                },
            )
            diff_path = scenario_dir / "diff.json"
            diff = json.loads(diff_path.read_text(encoding="utf-8"))
            diff["writer_events"] = len(events)
            diff["writer_candidate_pages"] = len(candidates)
            diff["writer_missed_pages"] = len(missed)
            json_dump(diff_path, diff)
            manifest_scenarios[target_name]["delta"] = diff
            print(f"  Writer trace: {len(events)} first-writer events, {len(missed)} candidate pages not hit.")

        remove_owned_memchecks(debug)
        remove_owned_breakpoints(debug)
        add_hash_manifest(temp_root, manifest)
        json_dump(temp_root / "manifest.json", manifest)
        print()
        print("Creating single self-contained oracle ZIP...")
        create_zip(temp_root, output)
        print(f"Retail oracle: {output.resolve()}")
        print(f"Size: {output.stat().st_size / (1024 * 1024):.1f} MiB")
        print("Capture complete. Send this one ZIP file for differential analysis.")
        return 0
    except (OracleError, CaptureError, OSError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        try:
            remove_owned_memchecks(debug)
            remove_owned_breakpoints(debug)
        except Exception:
            pass
        return 1
    finally:
        pine.close()
        if fast_reader is not None:
            fast_reader.close()
        shutil.rmtree(temp_root, ignore_errors=True)

def inspect_archive(path: Path) -> int:
    with zipfile.ZipFile(path, "r") as archive:
        manifest = json.loads(archive.read("manifest.json"))
        baseline = archive.read("baseline/ee_ram.bin")
        expected = manifest.get("baseline", {}).get("ee_ram_sha256")
        actual = sha256_bytes(baseline)
        if expected != actual:
            raise OracleError(f"Baseline SHA-256 mismatch: manifest={expected}, actual={actual}")
        print(f"Kind: {manifest.get('kind')}")
        print(f"Game: {manifest.get('game', {}).get('title')} | {manifest.get('game', {}).get('id')}")
        print(f"Baseline: {len(baseline)} bytes sha256={actual}")
        for scenario in manifest.get("scenarios", []):
            name = scenario["name"]
            delta = archive.read(f"scenarios/{name}/ee_ram.delta")
            reconstructed = decode_page_delta(baseline, delta)
            digest = sha256_bytes(reconstructed)
            expected_digest = scenario.get("delta", {}).get("post_ram_sha256")
            ok = digest == expected_digest
            print(
                f"{name:16s} pages={scenario.get('delta', {}).get('changed_pages')} "
                f"writers={scenario.get('delta', {}).get('writer_events')} reconstruct={'ok' if ok else 'FAIL'}"
            )
            if not ok:
                raise OracleError(f"Scenario {name!r} reconstruction hash mismatch.")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    cap = sub.add_parser("capture", help="capture the one-shot Level-0 Retail oracle")
    cap.add_argument("--host", default="127.0.0.1")
    cap.add_argument("--debug-port", type=int, default=21512)
    cap.add_argument("--pine-port", type=int, default=28011)
    cap.add_argument("--pcsx2-pid", type=int, help="Windows PCSX2 process id for fast read-only EEmem snapshots")
    cap.add_argument("--require-fast-ram", action="store_true", help="fail before capture instead of using the slow DebugServer RAM fallback")
    cap.add_argument("--allow-slow", action="store_true", help=argparse.SUPPRESS)
    cap.add_argument("--request-interval-ms", type=int, default=100)
    cap.add_argument("--savestate-slot", type=int, default=9)
    cap.add_argument("--savestate-settle", type=float, default=1.0)
    cap.add_argument("--arm-countdown", type=int, default=8)
    cap.add_argument("--scenario-countdown", type=int, default=3)
    cap.add_argument("--scenario-run-ms", type=int, default=DEFAULT_SCENARIO_RUN_MS)
    cap.add_argument("--writer-page-limit", type=int, default=DEFAULT_WRITER_PAGE_LIMIT)
    cap.add_argument("--writer-event-limit", type=int, default=DEFAULT_WRITER_EVENT_LIMIT)
    cap.add_argument("--writer-timeout", type=float, default=15.0)
    cap.add_argument("--no-speech", action="store_true", help="disable best-effort Windows spoken scenario cues")
    cap.add_argument("--no-beep", action="store_true", help="disable audible countdown/capture cues")
    cap.add_argument("--reach-timeout", type=float, default=30.0)
    cap.add_argument("--frame-timeout", type=float, default=30.0)
    cap.add_argument("--max-writer-events", type=int, default=2048)
    cap.add_argument("--scenarios", type=Path)
    cap.add_argument("--output", type=Path)

    inspect = sub.add_parser("inspect", help="validate/reconstruct every scenario in an oracle ZIP")
    inspect.add_argument("archive", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "capture":
            if args.request_interval_ms < 100:
                raise OracleError("--request-interval-ms must be at least 100 ms for the patched DebugServer.")
            if not 0 <= args.savestate_slot <= 9:
                raise OracleError("--savestate-slot must be 0..9.")
            if args.max_writer_events < 1:
                raise OracleError("--max-writer-events must be positive.")
            if args.arm_countdown < 1:
                raise OracleError("--arm-countdown must be positive.")
            if args.scenario_countdown < 2:
                raise OracleError("--scenario-countdown must be at least 2 so HOLD NOW has a stable cue.")
            if args.scenario_run_ms < 50 or args.scenario_run_ms > 5000:
                raise OracleError("--scenario-run-ms must be between 50 and 5000.")
            if args.writer_page_limit < 1 or args.writer_page_limit > 64:
                raise OracleError("--writer-page-limit must be between 1 and 64.")
            if args.writer_event_limit < 1 or args.writer_event_limit > 512:
                raise OracleError("--writer-event-limit must be between 1 and 512.")
            if args.writer_timeout <= 0:
                raise OracleError("--writer-timeout must be positive.")
            return capture(args)
        return inspect_archive(args.archive)
    except (OracleError, OSError, zipfile.BadZipFile, KeyError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
