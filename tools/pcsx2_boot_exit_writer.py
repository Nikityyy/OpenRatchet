#!/usr/bin/env python3
"""Prove the authentic Retail Boot-exit writer without memory watchpoints.

Phase-12 history narrowed the Boot exit to the transition where Retail raises
0x0015F5B0 from zero, escapes the loop at 0x001EBC2C, and returns to the outer
generation dispatcher at 0x0012DA00. The previously suspected fixed-address
store at 0x0021E880 is disproved by an accepted Retail breakpoint trace.

The generated Boot code exposes the missing indirect-address writer in
sub_0022E188:

  0x0022E190  sw $a0, -0x0A00($at)      -> 0x0015F600
  0x0022E198  sw $v0, -0x09E8($at)      -> 0x0015F618
  0x0022E19C  jr $ra
  0x0022E1A0  sw $v0, -0x7650($gp)      (delay slot)

At the Retail Boot ABI $gp is expected to be 0x00166C00, so the delay-slot
store destination is exactly 0x0015F5B0. This v4 oracle uses executable
breakpoints only. It stops immediately before that delay slot, captures $gp/$v0
and the pre-write state, then proves 0x15F5B0==1 at the Boot-loop escape and the
subsequent dispatcher return.

No memory memchecks, no guest writes, no snapshot loading, no native rebuild.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import time
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from pcsx2_sif_capture import format_address, parse_address
from pcsx2_level0_oracle import OracleDebugClient, OracleError, wait_until_paused

SCHEMA = 4
KIND = "RAC1_RETAIL_BOOT_EXIT_WRITER"
DEFAULT_TIMEOUT = 300.0

BOOT_EXIT_FLAG = 0x0015F5B0
PROCESSED_EDGES = 0x0013CB04
BRANCH_STATE_15F600 = 0x0015F600
BRANCH_STATE_15F618 = 0x0015F618
LOADER_STATE_15ED84 = 0x0015ED84
LOADER_STATE_15ED88 = 0x0015ED88
OVERLAY_STREAM_POINTER = 0x0015EE4C
GENERATION_PROBE_PC = 0x001EBA08

DISPROVED_FIXED_WRITER_PC = 0x0021E880
BRANCH_STATE_WRITE_PC = 0x0022E190
CANDIDATE_RETURN_PC = 0x0022E19C
BOOT_EXIT_WRITE_PC = 0x0022E1A0
BOOT_EXIT_GP_OFFSET = -0x7650
EXPECTED_BOOT_GP = 0x00166C00
BOOT_LOOP_ESCAPE_PC = 0x001EBC2C
DISPATCHER_RETURN_PC = 0x0012DA00

OWNED_PREFIX = "OpenRatchet boot-exit-writer-v4:"
STALE_PREFIXES = (
    OWNED_PREFIX,
    "OpenRatchet boot-exit-writer-v3:",
    "OpenRatchet boot-exit-path:",
    "OpenRatchet boot-exit-writer:",
    "OpenRatchet boot-chain:",
    "OpenRatchet oracle:",
)

TRACE_BREAKPOINTS: tuple[tuple[str, int], ...] = (
    ("gp-relative-writer-pre-delay", CANDIDATE_RETURN_PC),
    ("boot-loop-escape", BOOT_LOOP_ESCAPE_PC),
    ("dispatcher-return", DISPATCHER_RETURN_PC),
)

STATE_WORDS: tuple[tuple[str, int], ...] = (
    ("processed-edges", PROCESSED_EDGES),
    ("boot-exit-flag", BOOT_EXIT_FLAG),
    ("branch-state-15F600", BRANCH_STATE_15F600),
    ("branch-state-15F618", BRANCH_STATE_15F618),
    ("loader-state-15ED84", LOADER_STATE_15ED84),
    ("loader-state-15ED88", LOADER_STATE_15ED88),
    ("overlay-stream-pointer", OVERLAY_STREAM_POINTER),
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_boot_signature(oracle_zip: Path) -> tuple[bytes, dict[str, Any]]:
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
    if probe_pc != GENERATION_PROBE_PC:
        raise OracleError(
            f"oracle generation probe is {format_address(probe_pc)}, expected "
            f"{format_address(GENERATION_PROBE_PC)}"
        )
    try:
        boot = bytes.fromhex(str(generation["boot_elf_hex"]))
    except (KeyError, ValueError) as exc:
        raise OracleError("oracle Boot generation fingerprint is missing or malformed") from exc
    if not boot:
        raise OracleError("oracle Boot generation fingerprint is empty")
    return boot, manifest


def read_bytes(client: OracleDebugClient, address: int, length: int) -> bytes:
    payload = bytes.fromhex(client.read_memory("ee", address, length))
    if len(payload) != length:
        raise OracleError(
            f"DebugServer returned {len(payload)} bytes for {format_address(address)}+{length}, "
            f"expected {length}"
        )
    return payload


def read_u32(client: OracleDebugClient, address: int) -> int:
    return int.from_bytes(read_bytes(client, address, 4), "little")


def read_state(client: OracleDebugClient) -> dict[str, str]:
    return {name: format_address(read_u32(client, address)) for name, address in STATE_WORDS}


def read_generation_hex(client: OracleDebugClient, width: int) -> str:
    return read_bytes(client, GENERATION_PROBE_PC, width).hex()


def _is_owned(description: str) -> bool:
    return any(description.startswith(prefix) for prefix in STALE_PREFIXES)


def remove_owned_artifacts(client: OracleDebugClient) -> None:
    for bp in client.list_breakpoints("ee"):
        if _is_owned(str(bp.get("description", ""))):
            client.remove_breakpoint(
                "ee", parse_address(bp.get("address", 0), "breakpoint.address")
            )
    # Remove stale v1/v3 memchecks. v4 never creates one.
    for mc in client.list_memchecks("ee"):
        if not _is_owned(str(mc.get("description", ""))):
            continue
        start = parse_address(mc.get("start", 0), "memcheck.start")
        end = parse_address(mc.get("end", start + 4), "memcheck.end")
        client.remove_memcheck("ee", start, end)


def assert_no_foreign_debug_artifacts(client: OracleDebugClient) -> None:
    foreign_bps = [
        bp for bp in client.list_breakpoints("ee")
        if not _is_owned(str(bp.get("description", "")))
    ]
    foreign_mcs = [
        mc for mc in client.list_memchecks("ee")
        if not _is_owned(str(mc.get("description", "")))
    ]
    if foreign_bps or foreign_mcs:
        raise OracleError(
            "PCSX2 has unrelated debugger breakpoints/watchpoints armed. Remove them before capture "
            f"(breakpoints={len(foreign_bps)}, watchpoints={len(foreign_mcs)})."
        )


def breakpoint_description(name: str) -> str:
    return f"{OWNED_PREFIX}{name}"


def set_breakpoint(client: OracleDebugClient, address: int, name: str) -> None:
    client.request(
        "set_breakpoint",
        cpu="ee",
        address=format_address(address),
        condition="",
        description=breakpoint_description(name),
        temporary=False,
        enabled=True,
    )


def arm_trace(client: OracleDebugClient) -> None:
    remove_owned_artifacts(client)
    for name, address in TRACE_BREAKPOINTS:
        set_breakpoint(client, address, name)


def remove_breakpoint_if_present(client: OracleDebugClient, address: int) -> None:
    for bp in client.list_breakpoints("ee"):
        if (
            parse_address(bp.get("address", 0), "breakpoint.address") == address
            and _is_owned(str(bp.get("description", "")))
        ):
            client.remove_breakpoint("ee", address)
            return


def gpr_u32(registers: dict[str, Any], name: str) -> int:
    """Extract the low 32 bits of a named GPR from DebugServer category-0 output."""
    block = registers.get("GPR", registers)
    regs = block.get("regs", []) if isinstance(block, dict) else []
    for reg in regs:
        if str(reg.get("name", "")).lower() != name.lower():
            continue
        value = str(reg.get("value", ""))
        if len(value) < 8:
            raise OracleError(f"register {name} has malformed value {value!r}")
        try:
            return int(value[-8:], 16)
        except ValueError as exc:
            raise OracleError(f"register {name} has malformed value {value!r}") from exc
    raise OracleError(f"DebugServer GPR response does not contain register {name!r}")


def attribute_gp_relative_writer(registers: dict[str, Any]) -> dict[str, Any]:
    gp = gpr_u32(registers, "gp")
    v0 = gpr_u32(registers, "v0")
    ra = gpr_u32(registers, "ra")
    destination = (gp + BOOT_EXIT_GP_OFFSET) & 0xFFFFFFFF
    if destination != BOOT_EXIT_FLAG:
        raise OracleError(
            "0x22E1A0 gp-relative destination mismatch: "
            f"gp={format_address(gp)} offset=-0x7650 -> {format_address(destination)}, "
            f"expected {format_address(BOOT_EXIT_FLAG)}"
        )
    if v0 == 0:
        raise OracleError("0x22E1A0 would write zero to the Boot-exit flag; expected non-zero $v0")
    return {
        "trigger_pc": format_address(CANDIDATE_RETURN_PC),
        "writer_pc": format_address(BOOT_EXIT_WRITE_PC),
        "instruction": "sw $v0, -0x7650($gp) (JR $ra delay slot)",
        "gp": format_address(gp),
        "offset": "-0x7650",
        "destination": format_address(destination),
        "value": format_address(v0),
        "ra": format_address(ra),
        "expected_boot_gp": format_address(EXPECTED_BOOT_GP),
        "gp_matches_expected_boot_abi": gp == EXPECTED_BOOT_GP,
    }


def capture_context(
    client: OracleDebugClient,
    *,
    status: dict[str, Any],
    event: str,
) -> dict[str, Any]:
    pc = parse_address(status.get("pc", 0), "status.pc")
    return {
        "event": event,
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "pc": format_address(pc),
        "cycles": status.get("cycles"),
        "state": read_state(client),
        "gpr": client.read_registers("ee", 0),
        "backtrace": client.backtrace("ee", 24),
        "disassembly": client.disassemble("ee", max(0, pc - 24), 16),
    }


def remaining_timeout(started: float, total_timeout: float) -> float:
    remaining = total_timeout - (time.monotonic() - started)
    if remaining <= 0:
        raise OracleError(
            f"timed out after {total_timeout:g}s before the Retail Boot exit path completed"
        )
    return remaining


def wait_for_trace_breakpoint(
    client: OracleDebugClient,
    *,
    started: float,
    total_timeout: float,
) -> dict[str, Any]:
    unrelated = 0
    owned_addresses = {address for _, address in TRACE_BREAKPOINTS}
    while True:
        status = wait_until_paused(client, timeout=remaining_timeout(started, total_timeout))
        pc = parse_address(status.get("pc", 0), "status.pc")
        if pc in owned_addresses:
            return status
        unrelated += 1
        if unrelated <= 3:
            print(f"  Boot-exit v4 note: non-owned pause at {format_address(pc)}; resuming.")
        elif unrelated == 4:
            print("  Boot-exit v4 note: suppressing further non-owned pause messages.")
        client.resume("ee")


def write_archive(output: Path, trace: dict[str, Any], *, oracle_zip: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    readme = "# OpenRatchet Retail Boot-Exit Writer Trace v4\n\n"
    readme += (
        "This archive is external read-only Retail evidence. v4 uses executable breakpoints only; "
        "there are no memory memchecks. It captures `0x0022E19C` immediately before the JR delay "
        "slot at `0x0022E1A0`, where Retail executes `sw $v0,-0x7650($gp)`. The captured `$gp` "
        "is used to prove the effective destination is exactly `0x0015F5B0`.\n\n"
    )
    readme += (
        "The trace then proves the Boot-loop escape at `0x001EBC2C` with the flag non-zero and the "
        "outer dispatcher return at `0x0012DA00`. The tool never writes guest RAM, never loads a "
        "runtime snapshot, and is not a shipping dependency.\n"
    )
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.writestr("trace.json", json.dumps(trace, indent=2, sort_keys=True) + "\n")
        archive.writestr("README.md", readme)
        archive.writestr(
            "sources.json",
            json.dumps(
                {"oracle_zip": str(oracle_zip), "oracle_sha256": sha256_file(oracle_zip)},
                indent=2,
                sort_keys=True,
            ) + "\n",
        )


def capture(args: argparse.Namespace) -> int:
    if args.timeout <= 0:
        raise OracleError("--timeout must be positive")
    if args.request_interval_ms < 100:
        raise OracleError("--request-interval-ms must be at least 100 ms")

    boot_signature, oracle_manifest = load_boot_signature(args.oracle)
    client = OracleDebugClient(
        host=args.host,
        port=args.debug_port,
        minimum_request_interval=args.request_interval_ms / 1000.0,
    )

    status = client.status("ee")
    if not bool(status.get("alive", True)):
        raise OracleError("PCSX2 DebugServer is reachable but the EE VM is not alive")
    if not bool(status.get("paused", False)):
        raise OracleError("Pause Retail once while Boot code is still resident, then start this capture.")

    assert_no_foreign_debug_artifacts(client)
    remove_owned_artifacts(client)

    current_generation = read_generation_hex(client, len(boot_signature))
    if current_generation != boot_signature.hex():
        raise OracleError(
            "Boot-exit path capture must start while the exact Retail boot-ELF generation is "
            f"resident at {format_address(GENERATION_PROBE_PC)}. Restart Retail and pause earlier."
        )
    initial_flag = read_u32(client, BOOT_EXIT_FLAG)
    if initial_flag != 0:
        raise OracleError(
            f"Boot exit flag is already {format_address(initial_flag)}; restart Retail and pause before exit."
        )

    started = time.monotonic()
    events: list[dict[str, Any]] = []
    writer: dict[str, Any] | None = None
    writer_seen = False
    escape_seen = False
    dispatcher_seen = False
    complete = False

    try:
        arm_trace(client)
        client.resume("ee")

        while not dispatcher_seen:
            status = wait_for_trace_breakpoint(client, started=started, total_timeout=args.timeout)
            pc = parse_address(status.get("pc", 0), "status.pc")

            if pc == CANDIDATE_RETURN_PC:
                if writer_seen:
                    raise OracleError("0x22E19C writer pre-delay breakpoint executed more than once")
                context = capture_context(client, status=status, event="gp-relative-writer-pre-delay")
                pre_flag = parse_address(context["state"]["boot-exit-flag"], "writer.pre.boot-exit-flag")
                if pre_flag != 0:
                    raise OracleError(
                        "0x22E19C reached after Boot-exit flag was already non-zero; attribution is ambiguous"
                    )
                writer = attribute_gp_relative_writer(context["gpr"])
                context["writer"] = writer
                events.append(context)
                writer_seen = True
                print(
                    "  GP-relative Boot writer armed by Retail: "
                    f"trigger={format_address(pc)} writer={writer['writer_pc']} "
                    f"gp={writer['gp']} dst={writer['destination']} value={writer['value']}"
                )
                remove_breakpoint_if_present(client, CANDIDATE_RETURN_PC)
                client.resume("ee")
                continue

            if pc == BOOT_LOOP_ESCAPE_PC:
                if not writer_seen or writer is None:
                    raise OracleError(
                        "Boot loop escaped before the 0x22E19C/0x22E1A0 gp-relative writer path was observed"
                    )
                if escape_seen:
                    raise OracleError("Boot-loop escape breakpoint executed more than once")
                context = capture_context(client, status=status, event="boot-loop-escape")
                flag = parse_address(context["state"]["boot-exit-flag"], "escape.boot-exit-flag")
                if flag == 0:
                    raise OracleError("Boot loop reached 0x1EBC2C while 0x15F5B0 is still zero")
                context["attributed_writer_pc"] = writer["writer_pc"]
                events.append(context)
                escape_seen = True
                print(
                    "  Boot loop escaped: "
                    f"pc={format_address(pc)} flag={format_address(flag)} writer={writer['writer_pc']}"
                )
                remove_breakpoint_if_present(client, BOOT_LOOP_ESCAPE_PC)
                client.resume("ee")
                continue

            if pc == DISPATCHER_RETURN_PC:
                if not writer_seen or not escape_seen or writer is None:
                    raise OracleError("dispatcher return occurred before writer + Boot-loop escape proof")
                context = capture_context(client, status=status, event="dispatcher-return")
                context["attributed_writer_pc"] = writer["writer_pc"]
                events.append(context)
                dispatcher_seen = True
                print(
                    "  Dispatcher return reached: "
                    f"pc={format_address(pc)} writer={writer['writer_pc']}"
                )
                complete = True
                break

            raise OracleError(f"unexpected owned breakpoint stop at {format_address(pc)}")
    finally:
        try:
            remove_owned_artifacts(client)
        except Exception:
            pass
        if not complete:
            try:
                client.pause("ee")
            except Exception:
                pass

    final_state = events[-1]["state"] if events and "state" in events[-1] else read_state(client)
    trace = {
        "schema": SCHEMA,
        "kind": KIND,
        "status": "complete",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "policy": {
            "guest_memory_writes": False,
            "runtime_snapshot_loading": False,
            "memory_watchpoints": 0,
            "breakpoints": [format_address(address) for _, address in TRACE_BREAKPOINTS],
            "capture_strategy": "gp-relative-writer-plus-control-flow-breakpoints-only",
            "pcsx2_role": "external-reference-oracle-only",
        },
        "game": oracle_manifest.get("game", {}),
        "initial": {
            "generation_probe_pc": format_address(GENERATION_PROBE_PC),
            "generation_hex": current_generation,
            "boot_exit_flag": format_address(initial_flag),
        },
        "disproved_fixed_writer": format_address(DISPROVED_FIXED_WRITER_PC),
        "source_distinction": {
            "0x22E190": "writes a0 to 0x0015F600; NOT the Boot-exit flag",
            "0x22E1A0": "JR-delay-slot sw v0,-0x7650(gp); runtime gp proves destination 0x0015F5B0",
        },
        "writer": writer,
        "boot_loop_escape": format_address(BOOT_LOOP_ESCAPE_PC),
        "dispatcher_return": format_address(DISPATCHER_RETURN_PC),
        "events": events,
        "result": {
            "writer_pc": writer["writer_pc"] if writer else None,
            "writer_destination": writer["destination"] if writer else None,
            "writer_value": writer["value"] if writer else None,
            "writer_gp": writer["gp"] if writer else None,
            "writer_seen": writer_seen,
            "boot_loop_escape_seen": escape_seen,
            "dispatcher_return_seen": dispatcher_seen,
            "boot_exit_flag": final_state["boot-exit-flag"],
            "processed_edges": final_state["processed-edges"],
        },
    }
    write_archive(args.output, trace, oracle_zip=args.oracle)
    print(f"Retail Boot-exit writer trace v4 complete: {args.output}")
    print(
        "Writer: "
        f"{writer['writer_pc'] if writer else 'unknown'} -> "
        f"{writer['destination'] if writer else 'unknown'}; "
        f"loopEscape={int(escape_seen)} dispatcherReturn={int(dispatcher_seen)}"
    )
    return 0


def inspect(path: Path) -> int:
    try:
        with zipfile.ZipFile(path, "r") as archive:
            trace = json.loads(archive.read("trace.json"))
    except (OSError, KeyError, json.JSONDecodeError, zipfile.BadZipFile) as exc:
        raise OracleError(f"cannot read Boot-exit writer trace {path}: {exc}") from exc
    if trace.get("kind") != KIND or trace.get("schema") != SCHEMA:
        raise OracleError(
            f"unexpected trace identity kind={trace.get('kind')!r} schema={trace.get('schema')!r}"
        )
    result = trace.get("result", {})
    if not bool(result.get("writer_seen")):
        raise OracleError("trace does not execute the gp-relative Boot-exit writer path")
    if result.get("writer_pc") != format_address(BOOT_EXIT_WRITE_PC):
        raise OracleError(f"trace writer PC is {result.get('writer_pc')!r}, expected 0x0022E1A0")
    if result.get("writer_destination") != format_address(BOOT_EXIT_FLAG):
        raise OracleError("trace does not prove the writer destination is 0x0015F5B0")
    if not bool(result.get("boot_loop_escape_seen")):
        raise OracleError("trace does not reach the Retail Boot-loop escape")
    if not bool(result.get("dispatcher_return_seen")):
        raise OracleError("trace does not reach the Retail dispatcher return")
    print(f"Kind: {trace['kind']} schema={trace['schema']}")
    print(f"Writer PC: {result.get('writer_pc')}")
    print(f"Writer destination: {result.get('writer_destination')}")
    print(f"Writer value: {result.get('writer_value')}")
    print(f"Writer GP: {result.get('writer_gp')}")
    print(f"Boot loop escape seen: {int(bool(result.get('boot_loop_escape_seen')))}")
    print(f"Dispatcher return seen: {int(bool(result.get('dispatcher_return_seen')))}")
    print(f"Boot exit flag: {result.get('boot_exit_flag')}")
    print(f"Processed edges: {result.get('processed_edges')}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    cap = sub.add_parser("capture", help="prove the Retail gp-relative Boot-exit writer path")
    cap.add_argument("--oracle", type=Path, required=True, help="completed Level-0 Retail oracle ZIP")
    cap.add_argument("--output", type=Path, required=True)
    cap.add_argument("--host", default="127.0.0.1")
    cap.add_argument("--debug-port", type=int, default=21512)
    cap.add_argument("--request-interval-ms", type=int, default=100)
    cap.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)

    ins = sub.add_parser("inspect", help="validate a completed Boot-exit writer trace ZIP")
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
    except (OracleError, OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
