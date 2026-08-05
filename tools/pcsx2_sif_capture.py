#!/usr/bin/env python3
"""Manifest-driven, multi-breakpoint PCSX2 DebugServer capture helper."""

from __future__ import annotations

import argparse
import json
import re
import socket
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


CAPTURE_DESCRIPTION_PREFIX = "[OpenRatchet:SIFCAP]"


class CaptureError(RuntimeError):
    pass


def parse_address(value: Any, field: str) -> int:
    if isinstance(value, bool):
        raise CaptureError(f"{field} must be an address, not a boolean.")
    if isinstance(value, int):
        address = value
    elif isinstance(value, str):
        normalized = value.strip().lower().replace("0x0x", "0x")
        try:
            address = int(normalized, 0)
        except ValueError as exc:
            raise CaptureError(f"{field} is not a numeric address: {value!r}") from exc
    else:
        raise CaptureError(f"{field} must be an integer or numeric string.")
    if not 0 <= address <= 0xFFFFFFFF:
        raise CaptureError(f"{field} is outside the 32-bit address space.")
    return address


def format_address(address: int) -> str:
    return f"0x{address:08x}"


def validate_manifest(raw: Any) -> dict[str, Any]:
    if not isinstance(raw, dict):
        raise CaptureError("The capture manifest must be a JSON object.")
    if raw.get("schema") != 1:
        raise CaptureError("The capture manifest requires schema: 1.")

    name = raw.get("name")
    if not isinstance(name, str) or not name.strip():
        raise CaptureError("The capture manifest requires a non-empty name.")
    cpu = str(raw.get("cpu", "ee")).lower()
    if cpu not in {"ee", "iop"}:
        raise CaptureError("Manifest cpu must be 'ee' or 'iop'.")

    targets = raw.get("targets")
    if not isinstance(targets, list) or not targets:
        raise CaptureError("The capture manifest requires at least one target.")
    if len(targets) > 64:
        raise CaptureError("A capture manifest may contain at most 64 targets.")

    normalized_targets: list[dict[str, Any]] = []
    names: set[str] = set()
    for index, target in enumerate(targets):
        prefix = f"targets[{index}]"
        if not isinstance(target, dict):
            raise CaptureError(f"{prefix} must be an object.")
        target_name = target.get("name")
        if not isinstance(target_name, str) or not target_name.strip():
            raise CaptureError(f"{prefix}.name must be a non-empty string.")
        if target_name in names:
            raise CaptureError(f"Duplicate target name: {target_name!r}.")
        names.add(target_name)

        address = parse_address(target.get("address"), f"{prefix}.address")
        condition = target.get("condition", "")
        if not isinstance(condition, str):
            raise CaptureError(f"{prefix}.condition must be a string.")
        max_hits = target.get("max_hits", 1)
        if isinstance(max_hits, bool) or not isinstance(max_hits, int) or not 1 <= max_hits <= 100:
            raise CaptureError(f"{prefix}.max_hits must be an integer from 1 to 100.")

        categories = target.get("register_categories", [0])
        if not isinstance(categories, list) or not categories:
            raise CaptureError(f"{prefix}.register_categories must be a non-empty array.")
        if any(isinstance(item, bool) or not isinstance(item, int) or not -1 <= item <= 6
               for item in categories):
            raise CaptureError(f"{prefix}.register_categories contains an invalid category.")

        expressions = target.get("expressions", {})
        if not isinstance(expressions, dict) or any(
            not isinstance(key, str) or not key or not isinstance(value, str) or not value
            for key, value in expressions.items()
        ):
            raise CaptureError(f"{prefix}.expressions must map names to expression strings.")

        memory = target.get("memory", [])
        if not isinstance(memory, list) or len(memory) > 32:
            raise CaptureError(f"{prefix}.memory must be an array of at most 32 ranges.")
        normalized_memory: list[dict[str, Any]] = []
        memory_names: set[str] = set()
        for memory_index, region in enumerate(memory):
            region_prefix = f"{prefix}.memory[{memory_index}]"
            if not isinstance(region, dict):
                raise CaptureError(f"{region_prefix} must be an object.")
            region_name = region.get("name")
            if not isinstance(region_name, str) or not region_name:
                raise CaptureError(f"{region_prefix}.name must be a non-empty string.")
            if region_name in memory_names:
                raise CaptureError(f"Duplicate memory range name in {target_name!r}: {region_name!r}.")
            memory_names.add(region_name)
            region_address = region.get("address")
            if not isinstance(region_address, (str, int)) or isinstance(region_address, bool):
                raise CaptureError(f"{region_prefix}.address must be an address or expression.")
            length = region.get("length")
            if isinstance(length, bool) or not isinstance(length, int) or not 1 <= length <= 4096:
                raise CaptureError(f"{region_prefix}.length must be an integer from 1 to 4096.")
            normalized_memory.append({
                "name": region_name,
                "address": region_address,
                "length": length,
            })

        normalized_targets.append({
            "name": target_name,
            "address": address,
            "condition": condition,
            "max_hits": max_hits,
            "register_categories": categories,
            "expressions": expressions,
            "memory": normalized_memory,
        })

    timeout_seconds = raw.get("timeout_seconds", 120)
    poll_interval_ms = raw.get("poll_interval_ms", 20)
    if not isinstance(timeout_seconds, (int, float)) or isinstance(timeout_seconds, bool) or timeout_seconds <= 0:
        raise CaptureError("timeout_seconds must be positive.")
    if isinstance(poll_interval_ms, bool) or not isinstance(poll_interval_ms, int) or not 5 <= poll_interval_ms <= 1000:
        raise CaptureError("poll_interval_ms must be an integer from 5 to 1000.")

    return {
        "schema": 1,
        "name": name.strip(),
        "cpu": cpu,
        "timeout_seconds": float(timeout_seconds),
        "poll_interval_ms": poll_interval_ms,
        "cleanup": bool(raw.get("cleanup", True)),
        "targets": normalized_targets,
    }


@dataclass(slots=True)
class DebugServerClient:
    host: str = "127.0.0.1"
    port: int = 21512
    timeout: float = 5.0
    # The patched DebugServer uses detached workers for socket requests.  Keep
    # one command outstanding at a time and leave the worker enough time to
    # retire before opening the next connection.
    minimum_request_interval: float = 0.1
    _last_request_started: float | None = field(default=None, init=False, repr=False)

    def _wait_for_request_slot(self) -> None:
        if self._last_request_started is None:
            return
        remaining = self.minimum_request_interval - (time.monotonic() - self._last_request_started)
        if remaining > 0.0:
            time.sleep(remaining)

    def request(self, command: str, *, cpu: str = "ee", **params: Any) -> dict[str, Any]:
        payload = {"cmd": command, "cpu": cpu, **params}
        raw = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        self._wait_for_request_slot()
        self._last_request_started = time.monotonic()
        try:
            with socket.create_connection((self.host, self.port), timeout=self.timeout) as connection:
                connection.settimeout(self.timeout)
                connection.sendall(raw)
                chunks = bytearray()
                while b"\n" not in chunks:
                    chunk = connection.recv(65536)
                    if not chunk:
                        break
                    chunks.extend(chunk)
        except OSError as exc:
            raise CaptureError(
                f"Could not connect to PCSX2 DebugServer at {self.host}:{self.port}: {exc}"
            ) from exc
        if not chunks:
            raise CaptureError("PCSX2 DebugServer returned no response.")
        try:
            response = json.loads(bytes(chunks).split(b"\n", 1)[0])
        except json.JSONDecodeError as exc:
            raise CaptureError("PCSX2 DebugServer returned invalid JSON.") from exc
        if response.get("ok") is False:
            raise CaptureError(str(response.get("error", f"PCSX2 command {command!r} failed.")))
        return response

    def status(self, cpu: str) -> dict[str, Any]:
        response = self.request("status", cpu=cpu)
        return response.get("data", response)

    def list_breakpoints(self, cpu: str) -> list[dict[str, Any]]:
        return list(self.request("list_breakpoints", cpu=cpu).get("breakpoints", []))

    def set_breakpoint(self, cpu: str, target: dict[str, Any], description: str) -> None:
        self.request(
            "set_breakpoint",
            cpu=cpu,
            address=format_address(target["address"]),
            condition=target["condition"],
            description=description,
            temporary=False,
        )

    def remove_breakpoint(self, cpu: str, address: int) -> None:
        self.request("remove_breakpoint", cpu=cpu, address=format_address(address))

    def resume(self, cpu: str) -> None:
        self.request("resume", cpu=cpu)

    def read_registers(self, cpu: str, category: int) -> Any:
        response = self.request("read_registers", cpu=cpu, category=category)
        return response.get("data", response)

    def evaluate(self, cpu: str, expression: str) -> dict[str, Any]:
        return self.request("evaluate", cpu=cpu, expression=expression)

    def read_memory(self, cpu: str, address: int, length: int) -> str:
        response = self.request(
            "read_memory", cpu=cpu, address=format_address(address), length=length
        )
        value = response.get("hex")
        if not isinstance(value, str):
            raise CaptureError("PCSX2 read_memory response did not contain a hex string.")
        return value


def breakpoint_address(breakpoint: dict[str, Any]) -> int:
    return parse_address(breakpoint.get("address", 0), "breakpoint.address")


def arm_targets(client: DebugServerClient, manifest: dict[str, Any]) -> list[str]:
    cpu = manifest["cpu"]
    existing = client.list_breakpoints(cpu)
    existing_addresses = {breakpoint_address(item) for item in existing}
    armed: list[str] = []
    for target in manifest["targets"]:
        if target["address"] in existing_addresses:
            continue
        description = f"{CAPTURE_DESCRIPTION_PREFIX} {manifest['name']}:{target['name']}"
        client.set_breakpoint(cpu, target, description)
        existing_addresses.add(target["address"])
        armed.append(format_address(target["address"]))
    return armed


def cleanup_targets(client: DebugServerClient, manifest: dict[str, Any]) -> list[str]:
    cpu = manifest["cpu"]
    target_addresses = {target["address"] for target in manifest["targets"]}
    removed: list[str] = []
    for breakpoint in client.list_breakpoints(cpu):
        address = breakpoint_address(breakpoint)
        description = str(breakpoint.get("description", ""))
        if address in target_addresses and description.startswith(CAPTURE_DESCRIPTION_PREFIX):
            client.remove_breakpoint(cpu, address)
            removed.append(format_address(address))
    return removed


def resolve_expression_address(
    client: DebugServerClient, cpu: str, value: str | int
) -> tuple[int, dict[str, Any] | None]:
    if isinstance(value, int) or re.fullmatch(r"\s*(?:0[xX][0-9a-fA-F]+|\d+)\s*", value):
        return parse_address(value, "memory.address"), None
    result = client.evaluate(cpu, value)
    if result.get("ok") is False:
        raise CaptureError(f"Could not evaluate memory address {value!r}: {result.get('error')}")
    resolved = result.get("result", result.get("value", result.get("hex")))
    return parse_address(resolved, f"evaluated memory address {value!r}"), result


def capture_target(
    client: DebugServerClient,
    manifest: dict[str, Any],
    target: dict[str, Any],
    status: dict[str, Any],
    ordinal: int,
) -> dict[str, Any]:
    cpu = manifest["cpu"]
    registers = {
        str(category): client.read_registers(cpu, category)
        for category in target["register_categories"]
    }
    expressions: dict[str, Any] = {}
    for name, expression in target["expressions"].items():
        expressions[name] = {
            "expression": expression,
            "result": client.evaluate(cpu, expression),
        }
    memory: dict[str, Any] = {}
    for region in target["memory"]:
        address, evaluation = resolve_expression_address(client, cpu, region["address"])
        memory[region["name"]] = {
            "address": format_address(address),
            "length": region["length"],
            "hex": client.read_memory(cpu, address, region["length"]),
        }
        if evaluation is not None:
            memory[region["name"]]["address_expression"] = region["address"]
            memory[region["name"]]["address_evaluation"] = evaluation
    return {
        "ordinal": ordinal,
        "target": target["name"],
        "pc": format_address(parse_address(status.get("pc", 0), "status.pc")),
        "cycles": status.get("cycles"),
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "registers": registers,
        "expressions": expressions,
        "memory": memory,
    }


def run_capture(client: DebugServerClient, manifest: dict[str, Any]) -> dict[str, Any]:
    cpu = manifest["cpu"]
    target_by_address: dict[int, list[dict[str, Any]]] = {}
    for target in manifest["targets"]:
        target_by_address.setdefault(target["address"], []).append(target)
    hits = {target["name"]: 0 for target in manifest["targets"]}
    required_hits = sum(target["max_hits"] for target in manifest["targets"])
    transcript: dict[str, Any] = {
        "schema": 1,
        "manifest": manifest["name"],
        "cpu": cpu,
        "debug_server": f"{client.host}:{client.port}",
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "captures": [],
        "unexpected_stops": [],
        "complete": False,
    }
    deadline = time.monotonic() + manifest["timeout_seconds"]
    sleep_seconds = manifest["poll_interval_ms"] / 1000.0
    status = client.status(cpu)

    while len(transcript["captures"]) < required_hits:
        pc = parse_address(status.get("pc", 0), "status.pc")
        paused = bool(status.get("paused", False))
        pending = [
            target for target in target_by_address.get(pc, [])
            if hits[target["name"]] < target["max_hits"]
        ]
        if paused and pending:
            target = pending[0]
            transcript["captures"].append(
                capture_target(client, manifest, target, status, len(transcript["captures"]) + 1)
            )
            hits[target["name"]] += 1
            if len(transcript["captures"]) >= required_hits:
                break
            client.resume(cpu)
        elif paused:
            transcript["unexpected_stops"].append({
                "pc": format_address(pc),
                "cycles": status.get("cycles"),
            })
            client.resume(cpu)

        while time.monotonic() < deadline:
            time.sleep(sleep_seconds)
            status = client.status(cpu)
            if bool(status.get("paused", False)):
                break
        else:
            missing = [name for name, count in hits.items() if count == 0]
            transcript["missing_targets"] = missing
            raise CaptureError(
                f"PCSX2 did not complete the capture within {manifest['timeout_seconds']:g} seconds; "
                f"missing targets: {', '.join(missing) or 'additional hits'}"
            )

    transcript["complete"] = True
    transcript["finished_utc"] = datetime.now(timezone.utc).isoformat()
    return transcript


def default_output_path(manifest: dict[str, Any]) -> Path:
    safe_name = re.sub(r"[^A-Za-z0-9_.-]+", "-", manifest["name"]).strip("-") or "capture"
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    return Path("build") / "reference-captures" / f"{safe_name}-{stamp}.json"


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, help="JSON capture manifest")
    parser.add_argument("--output", type=Path, help="transcript path")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=21512)
    parser.add_argument(
        "--request-interval-ms",
        type=int,
        default=100,
        help="minimum spacing between DebugServer requests (minimum: 100 ms)",
    )
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--validate-only", action="store_true")
    modes.add_argument("--arm-only", action="store_true")
    modes.add_argument("--capture-only", action="store_true")
    args = parser.parse_args(argv)

    try:
        if args.request_interval_ms < 100:
            raise CaptureError("--request-interval-ms must be at least 100.")
        raw_manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        manifest = validate_manifest(raw_manifest)
        if args.validate_only:
            print(f"Valid capture manifest: {manifest['name']} ({len(manifest['targets'])} targets)")
            return 0

        client = DebugServerClient(
            args.host,
            args.port,
            minimum_request_interval=args.request_interval_ms / 1000.0,
        )
        initial_status = client.status(manifest["cpu"])
        if not bool(initial_status.get("alive", True)):
            raise CaptureError("PCSX2 DebugServer is connected but the VM is not alive.")

        armed: list[str] = []
        if not args.capture_only:
            armed = arm_targets(client, manifest)
        if args.arm_only:
            result = {
                "manifest": manifest["name"],
                "armed": armed,
                "status": initial_status,
            }
        else:
            result = run_capture(client, manifest)
            result["armed_by_process"] = armed
            if manifest["cleanup"]:
                result["removed_breakpoints"] = cleanup_targets(client, manifest)

        output = args.output or default_output_path(manifest)
        write_json(output, result)
        print(f"Capture output: {output.resolve()}")
        if not args.arm_only:
            print(f"Captured targets: {len(result['captures'])}; complete={result['complete']}")
        return 0
    except (CaptureError, OSError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
