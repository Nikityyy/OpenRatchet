#!/usr/bin/env python3
"""Capture the Retail R&C1 Boot -> Level-0 dispatcher chain with minimal disruption.

This is an external PCSX2 oracle tool. It never writes guest memory and is never a
shipping OpenRatchet dependency.

Earlier revisions used write memchecks on several loader-state words. Those memchecks
stopped PCSX2 on ordinary Retail writes and made the game effectively unplayable while
capturing. The current trace therefore uses exactly one low-frequency stable breakpoint:
``sub_0012D9D8``'s generation-call site at ``0x0012D9F8``. Retail reaches that site only
when the dispatcher is about to execute a generation. The tool samples the already-proved
loader state there, classifies the resident code bytes as Boot/WAD158/Level0, immediately
resumes intermediate generations, and leaves PCSX2 paused only when Level 0 is reached. WAD158
classification remains available as evidence, but the accepted normal New Game -> Veldin trace
proved that WAD158 is not a mandatory outer dispatcher generation.

If exact writer attribution is needed after this chain is captured, it must be done with a
separate targeted breakpoint trace derived from the first boundary-state divergence, not
with broad always-on memory watchpoints.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import time
import zipfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from pcsx2_sif_capture import CaptureError, format_address, parse_address
from pcsx2_level0_oracle import (
    OracleDebugClient,
    OracleError,
    remove_owned_breakpoints,
    remove_owned_memchecks,
    wait_until_paused,
)
from rac1_overlay_aot import parse_raw_overlay_container

SCHEMA = 2
KIND = "RAC1_RETAIL_BOOT_CHAIN_TRACE"
PROBE_PC = 0x001EBA08
DEFAULT_TIMEOUT = 300.0
DEFAULT_MAX_EVENTS = 16

# One stable dispatcher breakpoint is sufficient. The initial exact Boot signature is
# verified before resuming; subsequent hits identify the next generation immediately
# before its jalr. This avoids the stop storm caused by loader-state memchecks.
GENERATION_CALL_PC = 0x0012D9F8
TRACE_BREAKPOINTS: tuple[tuple[str, int], ...] = (
    ("generation-call", GENERATION_CALL_PC),
)


@dataclass(frozen=True, slots=True)
class StateSpec:
    name: str
    address: int
    size: int = 4


STATE_SPECS: tuple[StateSpec, ...] = (
    StateSpec("boot-exit-flag", 0x0015F5B0),
    StateSpec("branch-state-15F600", 0x0015F600),
    StateSpec("branch-state-15F618", 0x0015F618),
    StateSpec("loader-state-15ED84", 0x0015ED84),
    StateSpec("loader-state-15ED88", 0x0015ED88),
    StateSpec("overlay-stream-pointer", 0x0015EE4C),
    StateSpec("branch-state-13D364", 0x0013D364),
    StateSpec("branch-state-13D36C", 0x0013D36C),
)

# Read the state words in two contiguous requests rather than eight individual requests.
# This keeps each debugger stop short enough that menu/gameplay interaction remains usable.
STATE_BLOCKS: tuple[tuple[int, int], ...] = (
    (0x0013D364, 0x0013D370),
    (0x0015ED84, 0x0015F61C),
)

OWNED_PREFIX = "OpenRatchet boot-chain:"


@dataclass(frozen=True, slots=True)
class GenerationSignatures:
    boot: bytes
    wad158: bytes
    level0: bytes

    @property
    def width(self) -> int:
        widths = {len(self.boot), len(self.wad158), len(self.level0)}
        if len(widths) != 1 or 0 in widths:
            raise OracleError(f"generation signatures have inconsistent widths: {sorted(widths)}")
        return next(iter(widths))

    def classify(self, payload: bytes) -> str:
        if payload == self.boot:
            return "boot-elf"
        if payload == self.wad158:
            return "wad_158"
        if payload == self.level0:
            return "level_00"
        return "unknown"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _load_oracle_generation(oracle_zip: Path) -> tuple[bytes, bytes, dict[str, Any]]:
    try:
        with zipfile.ZipFile(oracle_zip, "r") as archive:
            manifest = json.loads(archive.read("manifest.json"))
            generation = json.loads(archive.read("baseline/code_generation.json"))
    except (OSError, KeyError, json.JSONDecodeError, zipfile.BadZipFile) as exc:
        raise OracleError(f"cannot read Retail oracle {oracle_zip}: {exc}") from exc

    if manifest.get("kind") != "RAC1_RETAIL_ORACLE_LEVEL0":
        raise OracleError(
            f"oracle kind is {manifest.get('kind')!r}, expected 'RAC1_RETAIL_ORACLE_LEVEL0'"
        )
    probe_pc = parse_address(generation.get("pc", 0), "code_generation.pc")
    if probe_pc != PROBE_PC:
        raise OracleError(
            f"oracle generation probe is {format_address(probe_pc)}, expected {format_address(PROBE_PC)}"
        )
    try:
        boot = bytes.fromhex(str(generation["boot_elf_hex"]))
        level0 = bytes.fromhex(str(generation["level0_overlay_hex"]))
    except (KeyError, ValueError) as exc:
        raise OracleError("oracle generation fingerprints are missing or malformed") from exc
    if not boot or len(boot) != len(level0):
        raise OracleError("oracle Boot/Level-0 generation signatures disagree in length")
    return boot, level0, manifest


def load_generation_signatures(
    oracle_zip: Path, wad158_path: Path
) -> tuple[GenerationSignatures, dict[str, Any]]:
    boot, level0, manifest = _load_oracle_generation(oracle_zip)
    try:
        wad = wad158_path.read_bytes()
    except OSError as exc:
        raise OracleError(f"cannot read WAD158 {wad158_path}: {exc}") from exc
    image = parse_raw_overlay_container(wad, 158)
    executable = image.executable_segment
    width = len(boot)
    if not (executable.destination <= PROBE_PC < executable.end):
        raise OracleError(
            f"WAD158 executable does not contain generation probe {format_address(PROBE_PC)}"
        )
    offset = PROBE_PC - executable.destination
    if offset + width > len(executable.payload):
        raise OracleError("WAD158 generation signature crosses the executable segment boundary")
    wad158 = executable.payload[offset:offset + width]
    signatures = GenerationSignatures(boot=boot, wad158=wad158, level0=level0)
    _ = signatures.width
    if len({signatures.boot, signatures.wad158, signatures.level0}) != 3:
        raise OracleError("generation probe does not distinguish Boot, WAD158 and Level 0")
    return signatures, manifest


def read_generation(client: OracleDebugClient, signatures: GenerationSignatures) -> dict[str, Any]:
    raw = bytes.fromhex(client.read_memory("ee", PROBE_PC, signatures.width))
    if len(raw) != signatures.width:
        raise OracleError(
            f"DebugServer returned {len(raw)} generation bytes, expected {signatures.width}"
        )
    return {
        "generation": signatures.classify(raw),
        "pc": format_address(PROBE_PC),
        "hex": raw.hex(),
        "sha256": hashlib.sha256(raw).hexdigest(),
    }


def _read_block(client: OracleDebugClient, start: int, end: int) -> bytes:
    payload = bytes.fromhex(client.read_memory("ee", start, end - start))
    if len(payload) != end - start:
        raise OracleError(
            f"read of {format_address(start)}..{format_address(end)} returned "
            f"{len(payload)} bytes, expected {end - start}"
        )
    return payload


def decode_state_values(blocks: dict[tuple[int, int], bytes]) -> dict[str, str]:
    result: dict[str, str] = {}
    for spec in STATE_SPECS:
        owner: tuple[int, int] | None = None
        for start, end in STATE_BLOCKS:
            if start <= spec.address and spec.address + spec.size <= end:
                owner = (start, end)
                break
        if owner is None or owner not in blocks:
            raise OracleError(f"state word {spec.name} is outside the sampled blocks")
        start, _ = owner
        payload = blocks[owner]
        offset = spec.address - start
        raw = payload[offset:offset + spec.size]
        if len(raw) != spec.size:
            raise OracleError(f"sample for {spec.name} is truncated")
        value = int.from_bytes(raw, "little")
        result[spec.name] = format_address(value)
    return result


def read_state_values(client: OracleDebugClient) -> dict[str, str]:
    blocks = {(start, end): _read_block(client, start, end) for start, end in STATE_BLOCKS}
    return decode_state_values(blocks)


def breakpoint_description(name: str) -> str:
    return f"{OWNED_PREFIX}{name}"


def remove_trace_artifacts(client: OracleDebugClient) -> None:
    # Remove both current breakpoints and stale memchecks from the rejected v1 trace.
    for bp in client.list_breakpoints("ee"):
        if str(bp.get("description", "")).startswith(OWNED_PREFIX):
            client.remove_breakpoint("ee", parse_address(bp.get("address", 0), "breakpoint.address"))
    for mc in client.list_memchecks("ee"):
        if not str(mc.get("description", "")).startswith(OWNED_PREFIX):
            continue
        start = parse_address(mc.get("start", 0), "memcheck.start")
        end = parse_address(mc.get("end", start + 4), "memcheck.end")
        client.remove_memcheck("ee", start, end)


def arm_trace(client: OracleDebugClient) -> None:
    remove_trace_artifacts(client)
    existing = {
        parse_address(item.get("address", 0), "breakpoint.address")
        for item in client.list_breakpoints("ee")
    }
    for name, address in TRACE_BREAKPOINTS:
        if address in existing:
            continue
        client.request(
            "set_breakpoint",
            cpu="ee",
            address=format_address(address),
            condition="",
            description=breakpoint_description(name),
            temporary=False,
            enabled=True,
        )


def wait_for_next_trace_stop(
    client: OracleDebugClient,
    *,
    capture_started: float,
    total_timeout: float,
) -> dict[str, Any]:
    remaining = total_timeout - (time.monotonic() - capture_started)
    if remaining <= 0:
        raise OracleError(f"timed out after {total_timeout:g}s before Level-0 became resident")
    return wait_until_paused(client, timeout=remaining)


def capture_boundary_event(
    client: OracleDebugClient,
    *,
    ordinal: int,
    status: dict[str, Any],
    signatures: GenerationSignatures,
) -> dict[str, Any]:
    # Fast boundary capture: three memory requests + one register request. No backtrace,
    # disassembly or per-word requests while the user is navigating the game.
    generation = read_generation(client, signatures)
    state_values = read_state_values(client)
    registers = client.read_registers("ee", 0)
    return {
        "ordinal": ordinal,
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "reason": "generation-call",
        "pc": format_address(parse_address(status.get("pc", 0), "status.pc")),
        "cycles": status.get("cycles"),
        "generation": generation,
        "state_values": state_values,
        "gpr": registers if isinstance(registers, dict) else {"data": registers},
    }


def write_trace_archive(
    output: Path,
    trace: dict[str, Any],
    *,
    oracle_zip: Path,
    wad158_path: Path,
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    readme = "# OpenRatchet Retail Boot-Chain Trace\n\n"
    readme += "This archive is investigation evidence only. It was captured read-only through PCSX2's debugger.\n"
    readme += "It is not a runtime snapshot and must never be consumed by shipping OpenRatchet.\n\n"
    readme += (
        "`trace.json` contains exact Boot/WAD158/Level-0 generation classifications and compact "
        "loader-state snapshots captured only at the stable generation-call boundary. No memory "
        "watchpoints were armed, so ordinary Retail execution remains interactive.\n"
    )
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.writestr("trace.json", json.dumps(trace, indent=2, sort_keys=True) + "\n")
        archive.writestr("README.md", readme)
        archive.writestr(
            "sources.json",
            json.dumps(
                {
                    "oracle_zip": str(oracle_zip),
                    "oracle_sha256": sha256_file(oracle_zip),
                    "wad158": str(wad158_path),
                    "wad158_sha256": sha256_file(wad158_path),
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
        )


def capture(args: argparse.Namespace) -> int:
    signatures, oracle_manifest = load_generation_signatures(args.oracle, args.wad158)
    client = OracleDebugClient(
        host=args.host,
        port=args.debug_port,
        minimum_request_interval=args.request_interval_ms / 1000.0,
    )
    if args.request_interval_ms < 100:
        raise OracleError("--request-interval-ms must be at least 100 ms")
    if args.max_events < 1:
        raise OracleError("--max-events must be positive")
    if args.timeout <= 0:
        raise OracleError("--timeout must be positive")

    status = client.status("ee")
    if not bool(status.get("alive", True)):
        raise OracleError("PCSX2 DebugServer is reachable but the EE VM is not alive")
    if not bool(status.get("paused", False)):
        client.pause("ee")
        status = wait_until_paused(client, timeout=5.0)

    foreign_bps = [
        bp for bp in client.list_breakpoints("ee")
        if not str(bp.get("description", "")).startswith(OWNED_PREFIX)
        and not str(bp.get("description", "")).startswith("OpenRatchet oracle:")
    ]
    foreign_mcs = [
        mc for mc in client.list_memchecks("ee")
        if not str(mc.get("description", "")).startswith(OWNED_PREFIX)
        and not str(mc.get("description", "")).startswith("OpenRatchet oracle:")
    ]
    if foreign_bps or foreign_mcs:
        raise OracleError(
            "PCSX2 has unrelated debugger breakpoints/watchpoints armed; remove them before capture "
            f"(breakpoints={len(foreign_bps)}, watchpoints={len(foreign_mcs)})"
        )

    # Clean stale artifacts from both the Level-0 oracle and the rejected memcheck trace,
    # then require an exact Boot-generation start.
    remove_owned_breakpoints(client)
    remove_owned_memchecks(client)
    remove_trace_artifacts(client)
    initial_generation = read_generation(client, signatures)
    if initial_generation["generation"] != "boot-elf":
        raise OracleError(
            "Boot-chain capture must be armed while the exact Retail boot-ELF generation is resident; "
            f"observed {initial_generation['generation']} at {format_address(PROBE_PC)}. "
            "Restart Retail from boot before retrying."
        )

    events: list[dict[str, Any]] = []
    transitions: list[dict[str, Any]] = [
        {"ordinal": 0, "generation": "boot-elf", "pc": initial_generation["pc"]}
    ]
    last_generation = "boot-elf"
    started = time.monotonic()
    complete = False
    unexpected_stops = 0

    try:
        arm_trace(client)
        client.resume("ee")
        while len(events) < args.max_events:
            status = wait_for_next_trace_stop(
                client,
                capture_started=started,
                total_timeout=args.timeout,
            )
            pc = parse_address(status.get("pc", 0), "status.pc")

            # With no memchecks armed, ordinary Retail stores cannot stop the VM anymore.
            # If PCSX2 reports an internal/foreign stop despite the preflight check, resume it
            # immediately without expensive evidence collection so the game remains usable.
            if pc != GENERATION_CALL_PC:
                unexpected_stops += 1
                if unexpected_stops <= 3:
                    print(
                        "  Trace note: non-trace PCSX2 pause at "
                        f"{format_address(pc)}; resuming immediately."
                    )
                elif unexpected_stops == 4:
                    print("  Trace note: suppressing further non-trace pause messages.")
                client.resume("ee")
                continue

            event = capture_boundary_event(
                client,
                ordinal=len(events) + 1,
                status=status,
                signatures=signatures,
            )
            events.append(event)
            generation = event["generation"]["generation"]
            print(
                f"  Trace boundary #{event['ordinal']}: {generation} "
                f"at {event['pc']}"
            )

            if generation != last_generation:
                transitions.append(
                    {
                        "ordinal": event["ordinal"],
                        "generation": generation,
                        "pc": event["pc"],
                        "reason": "generation-call",
                    }
                )
                last_generation = generation

            if generation == "level_00":
                complete = True
                break

            # Intermediate boundary captures are intentionally compact; resume immediately.
            client.resume("ee")

        if not complete:
            raise OracleError(
                f"event limit ({args.max_events}) reached before the exact Level-0 generation appeared"
            )
    finally:
        # Leave the successful Level-0 capture paused so the caller has a stable final state.
        if not complete:
            try:
                client.pause("ee")
            except Exception:
                pass
        try:
            remove_trace_artifacts(client)
        except Exception:
            pass

    trace = {
        "schema": SCHEMA,
        "kind": KIND,
        "status": "complete",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "policy": {
            "guest_memory_writes": False,
            "runtime_snapshot_loading": False,
            "memory_watchpoints": False,
            "pcsx2_role": "external-reference-oracle-only",
            "capture_strategy": "stable-generation-call-breakpoint-only",
        },
        "game": oracle_manifest.get("game", {}),
        "probe": {
            "pc": format_address(PROBE_PC),
            "boot_hex": signatures.boot.hex(),
            "wad158_hex": signatures.wad158.hex(),
            "level0_hex": signatures.level0.hex(),
        },
        "state_specs": [
            {"name": spec.name, "address": format_address(spec.address), "size": spec.size}
            for spec in STATE_SPECS
        ],
        "trace_breakpoints": [
            {"name": name, "address": format_address(address)}
            for name, address in TRACE_BREAKPOINTS
        ],
        "initial_generation": initial_generation,
        "generation_transitions": transitions,
        "event_count": len(events),
        "unexpected_stop_count": unexpected_stops,
        "events": events,
    }
    write_trace_archive(args.output, trace, oracle_zip=args.oracle, wad158_path=args.wad158)
    print(f"Retail Boot-chain trace complete: {args.output}")
    print("Generation transitions: " + " -> ".join(item["generation"] for item in transitions))
    print(f"Events: {len(events)}")
    return 0


def inspect(path: Path) -> int:
    try:
        with zipfile.ZipFile(path, "r") as archive:
            trace = json.loads(archive.read("trace.json"))
    except (OSError, KeyError, json.JSONDecodeError, zipfile.BadZipFile) as exc:
        raise OracleError(f"cannot read Boot-chain trace {path}: {exc}") from exc
    if trace.get("kind") != KIND or trace.get("schema") != SCHEMA:
        raise OracleError(
            f"unexpected trace identity kind={trace.get('kind')!r} schema={trace.get('schema')!r}"
        )
    transitions = [str(item.get("generation")) for item in trace.get("generation_transitions", [])]
    if not transitions or transitions[0] != "boot-elf" or transitions[-1] != "level_00":
        raise OracleError(f"trace does not span Boot -> Level 0: {transitions}")
    # The accepted Retail New Game -> Veldin trace reaches the stable generation-call
    # boundary twice: Boot first, then Level 0 directly. WAD158 remains a known exact
    # overlay artifact/signature and may appear on other paths, but is not mandatory
    # at this dispatcher boundary. If present, it must occur before Level 0.
    if "wad_158" in transitions and transitions.index("wad_158") > transitions.index("level_00"):
        raise OracleError(f"WAD158 appeared after Level 0: {transitions}")
    print(f"Kind: {trace['kind']} schema={trace['schema']}")
    print("Generation transitions: " + " -> ".join(transitions))
    print(f"Events: {trace.get('event_count', len(trace.get('events', [])))}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    cap = sub.add_parser("capture", help="capture Retail Boot -> Level-0 dispatcher lifecycle")
    cap.add_argument("--oracle", type=Path, required=True, help="completed Level-0 Retail oracle ZIP")
    cap.add_argument(
        "--wad158",
        type=Path,
        default=Path("build/extracted/wads/wad_158.wad"),
        help="exact extracted Retail WAD158 record stream",
    )
    cap.add_argument("--output", type=Path, required=True)
    cap.add_argument("--host", default="127.0.0.1")
    cap.add_argument("--debug-port", type=int, default=21512)
    cap.add_argument("--request-interval-ms", type=int, default=100)
    cap.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    cap.add_argument("--max-events", type=int, default=DEFAULT_MAX_EVENTS)

    ins = sub.add_parser("inspect", help="validate a completed Boot-chain trace ZIP")
    ins.add_argument("archive", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "capture":
            return capture(args)
        if args.command == "inspect":
            return inspect(args.archive)
        raise OracleError(f"unsupported command {args.command!r}")
    except (OracleError, CaptureError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
