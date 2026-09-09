from __future__ import annotations

import json
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pcsx2_boot_chain_trace as trace


class FakeStateClient:
    def __init__(self, blocks: dict[tuple[int, int], bytes]) -> None:
        self.blocks = blocks
        self.reads: list[tuple[int, int]] = []

    def read_memory(self, cpu: str, address: int, length: int) -> str:
        self.assert_cpu(cpu)
        self.reads.append((address, length))
        for (start, end), payload in self.blocks.items():
            if address == start and length == end - start:
                return payload.hex()
        raise AssertionError(f"unexpected read {address:#x}+{length:#x}")

    @staticmethod
    def assert_cpu(cpu: str) -> None:
        if cpu != "ee":
            raise AssertionError(cpu)


class FakeArmClient:
    def __init__(self) -> None:
        self.breakpoints: list[dict[str, object]] = []
        self.memchecks: list[dict[str, object]] = []
        self.requests: list[tuple[str, dict[str, object]]] = []

    def list_breakpoints(self, cpu: str) -> list[dict[str, object]]:
        return list(self.breakpoints)

    def list_memchecks(self, cpu: str) -> list[dict[str, object]]:
        return list(self.memchecks)

    def remove_breakpoint(self, cpu: str, address: int) -> None:
        self.breakpoints = [bp for bp in self.breakpoints if int(str(bp["address"]), 16) != address]

    def remove_memcheck(self, cpu: str, start: int, end: int) -> None:
        self.memchecks = [
            mc for mc in self.memchecks
            if not (int(str(mc["start"]), 16) == start and int(str(mc["end"]), 16) == end)
        ]

    def request(self, command: str, **kwargs: object) -> dict[str, object]:
        self.requests.append((command, kwargs))
        return {"ok": True}


class BootChainTraceTests(unittest.TestCase):
    def test_generation_classifier_distinguishes_all_three_generations(self) -> None:
        signatures = trace.GenerationSignatures(
            boot=b"B" * 32,
            wad158=b"W" * 32,
            level0=b"L" * 32,
        )
        self.assertEqual(signatures.width, 32)
        self.assertEqual(signatures.classify(b"B" * 32), "boot-elf")
        self.assertEqual(signatures.classify(b"W" * 32), "wad_158")
        self.assertEqual(signatures.classify(b"L" * 32), "level_00")
        self.assertEqual(signatures.classify(b"?" * 32), "unknown")

    def test_load_signatures_uses_oracle_and_exact_wad158_payload(self) -> None:
        boot = bytes(range(32))
        level0 = bytes((i + 64) & 0xFF for i in range(32))
        wad_payload = bytearray(64)
        wad_signature = bytes((i + 128) & 0xFF for i in range(32))
        wad_payload[8:40] = wad_signature

        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            oracle_zip = temp / "oracle.zip"
            with zipfile.ZipFile(oracle_zip, "w") as archive:
                archive.writestr(
                    "manifest.json",
                    json.dumps({"kind": "RAC1_RETAIL_ORACLE_LEVEL0", "game": {"id": "SCUS-97199"}}),
                )
                archive.writestr(
                    "baseline/code_generation.json",
                    json.dumps(
                        {
                            "pc": trace.format_address(trace.PROBE_PC),
                            "boot_elf_hex": boot.hex(),
                            "level0_overlay_hex": level0.hex(),
                        }
                    ),
                )

            wad = temp / "wad_158.wad"
            record = struct.pack(
                "<IIII",
                trace.PROBE_PC - 8,
                len(wad_payload),
                1,
                trace.PROBE_PC - 8,
            )
            wad.write_bytes(record + bytes(wad_payload) + bytes(32))

            signatures, manifest = trace.load_generation_signatures(oracle_zip, wad)
            self.assertEqual(signatures.boot, boot)
            self.assertEqual(signatures.wad158, wad_signature)
            self.assertEqual(signatures.level0, level0)
            self.assertEqual(manifest["game"]["id"], "SCUS-97199")

    def test_state_sampling_uses_exactly_two_contiguous_reads(self) -> None:
        blocks: dict[tuple[int, int], bytes] = {}
        expected: dict[str, str] = {}
        values = {
            "boot-exit-flag": 0x11,
            "branch-state-15F600": 0x22,
            "branch-state-15F618": 0x33,
            "loader-state-15ED84": 0x44,
            "loader-state-15ED88": 0x55,
            "overlay-stream-pointer": 0x66,
            "branch-state-13D364": 0x77,
            "branch-state-13D36C": 0x88,
        }
        for start, end in trace.STATE_BLOCKS:
            payload = bytearray(end - start)
            for spec in trace.STATE_SPECS:
                if start <= spec.address and spec.address + spec.size <= end:
                    value = values[spec.name]
                    offset = spec.address - start
                    payload[offset:offset + 4] = value.to_bytes(4, "little")
                    expected[spec.name] = trace.format_address(value)
            blocks[(start, end)] = bytes(payload)

        client = FakeStateClient(blocks)
        actual = trace.read_state_values(client)
        self.assertEqual(actual, expected)
        self.assertEqual(len(client.reads), 2)
        self.assertEqual(
            client.reads,
            [(start, end - start) for start, end in trace.STATE_BLOCKS],
        )

    def test_arm_trace_sets_one_breakpoint_and_no_memchecks(self) -> None:
        client = FakeArmClient()
        # Simulate stale v1 watchpoint; arm_trace must remove it and never replace it.
        client.memchecks.append(
            {
                "description": trace.breakpoint_description("loader-state-15ED84"),
                "start": "0x0015ed84",
                "end": "0x0015ed88",
            }
        )
        trace.arm_trace(client)
        set_breakpoint = [entry for entry in client.requests if entry[0] == "set_breakpoint"]
        self.assertEqual(len(set_breakpoint), 1)
        self.assertEqual(
            set_breakpoint[0][1]["address"],
            trace.format_address(trace.GENERATION_CALL_PC),
        )
        self.assertEqual(client.memchecks, [])
        self.assertFalse(any(command == "set_memcheck" for command, _ in client.requests))

    def test_wait_for_next_trace_stop_uses_remaining_global_timeout(self) -> None:
        client = object()
        with (
            mock.patch.object(trace.time, "monotonic", return_value=112.5),
            mock.patch.object(
                trace,
                "wait_until_paused",
                return_value={"alive": True, "paused": True, "pc": "0x0012d9f8"},
            ) as wait_mock,
        ):
            status = trace.wait_for_next_trace_stop(
                client,
                capture_started=100.0,
                total_timeout=300.0,
            )
        self.assertTrue(status["paused"])
        wait_mock.assert_called_once_with(client, timeout=287.5)

    def test_wait_for_next_trace_stop_fails_only_at_global_deadline(self) -> None:
        with mock.patch.object(trace.time, "monotonic", return_value=400.0):
            with self.assertRaisesRegex(trace.OracleError, "timed out after 300s"):
                trace.wait_for_next_trace_stop(
                    object(),
                    capture_started=100.0,
                    total_timeout=300.0,
                )

    def test_inspect_accepts_retail_direct_boot_to_level0_and_optional_wad158(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            direct = Path(temp_dir) / "direct.zip"
            with zipfile.ZipFile(direct, "w") as archive:
                archive.writestr(
                    "trace.json",
                    json.dumps(
                        {
                            "kind": trace.KIND,
                            "schema": trace.SCHEMA,
                            "generation_transitions": [
                                {"generation": "boot-elf"},
                                {"generation": "level_00"},
                            ],
                            "events": [],
                        }
                    ),
                )
            self.assertEqual(trace.inspect(direct), 0)

            with_wad = Path(temp_dir) / "with_wad.zip"
            with zipfile.ZipFile(with_wad, "w") as archive:
                archive.writestr(
                    "trace.json",
                    json.dumps(
                        {
                            "kind": trace.KIND,
                            "schema": trace.SCHEMA,
                            "generation_transitions": [
                                {"generation": "boot-elf"},
                                {"generation": "wad_158"},
                                {"generation": "level_00"},
                            ],
                            "events": [],
                        }
                    ),
                )
            self.assertEqual(trace.inspect(with_wad), 0)

            bad = Path(temp_dir) / "bad.zip"
            with zipfile.ZipFile(bad, "w") as archive:
                archive.writestr(
                    "trace.json",
                    json.dumps(
                        {
                            "kind": trace.KIND,
                            "schema": trace.SCHEMA,
                            "generation_transitions": [
                                {"generation": "boot-elf"},
                                {"generation": "level_00"},
                                {"generation": "wad_158"},
                            ],
                        }
                    ),
                )
            with self.assertRaisesRegex(trace.OracleError, "does not span Boot -> Level 0"):
                trace.inspect(bad)

    def test_default_trace_event_bound_is_small(self) -> None:
        self.assertEqual(trace.DEFAULT_MAX_EVENTS, 16)
        self.assertEqual(trace.TRACE_BREAKPOINTS, (("generation-call", 0x0012D9F8),))


if __name__ == "__main__":
    unittest.main()
