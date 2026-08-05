from __future__ import annotations

import sys
import unittest
from pathlib import Path
from typing import Any
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pcsx2_sif_capture as capture


def make_manifest() -> dict[str, Any]:
    return capture.validate_manifest(
        {
            "schema": 1,
            "name": "two-stop-test",
            "cpu": "ee",
            "timeout_seconds": 1,
            "poll_interval_ms": 5,
            "targets": [
                {
                    "name": "before",
                    "address": "0x1000",
                    "expressions": {"client": "a0"},
                    "memory": [
                        {"name": "packet", "address": "a0 + 0x20", "length": 8}
                    ],
                },
                {
                    "name": "after",
                    "address": "0x2000",
                    "memory": [
                        {"name": "receive", "address": "0x3000", "length": 4}
                    ],
                },
            ],
        }
    )


class FakeClient:
    host = "fake"
    port = 21512

    def __init__(self) -> None:
        self.statuses = [
            {"alive": True, "paused": True, "pc": "0x1000", "cycles": 10},
            {"alive": True, "paused": True, "pc": "0x2000", "cycles": 20},
        ]
        self.status_index = 0
        self.breakpoints = [
            {"address": "0x1000", "description": "user breakpoint"}
        ]
        self.removed: list[int] = []

    def status(self, _cpu: str) -> dict[str, Any]:
        return self.statuses[self.status_index]

    def resume(self, _cpu: str) -> None:
        if self.status_index + 1 < len(self.statuses):
            self.status_index += 1

    def list_breakpoints(self, _cpu: str) -> list[dict[str, Any]]:
        return list(self.breakpoints)

    def set_breakpoint(
        self, _cpu: str, target: dict[str, Any], description: str
    ) -> None:
        self.breakpoints.append(
            {"address": capture.format_address(target["address"]), "description": description}
        )

    def remove_breakpoint(self, _cpu: str, address: int) -> None:
        self.removed.append(address)
        self.breakpoints = [
            item for item in self.breakpoints
            if capture.breakpoint_address(item) != address
        ]

    def read_registers(self, _cpu: str, category: int) -> dict[str, Any]:
        return {"category": category, "a0": "0x4000"}

    def evaluate(self, _cpu: str, expression: str) -> dict[str, Any]:
        values = {"a0": 0x4000, "a0 + 0x20": 0x4020}
        value = values[expression]
        return {"ok": True, "result": value, "hex": hex(value)}

    def read_memory(self, _cpu: str, address: int, length: int) -> str:
        return bytes((address + offset) & 0xFF for offset in range(length)).hex()


class CaptureTests(unittest.TestCase):
    class FakeSocket:
        def __init__(self) -> None:
            self.sent = bytearray()
            self._responses = [b'{"ok":true}\n']

        def __enter__(self) -> "CaptureTests.FakeSocket":
            return self

        def __exit__(self, _exc_type: object, _exc: object, _traceback: object) -> None:
            return None

        def settimeout(self, _timeout: float) -> None:
            return None

        def sendall(self, data: bytes) -> None:
            self.sent.extend(data)

        def recv(self, _size: int) -> bytes:
            return self._responses.pop(0) if self._responses else b""

    def test_manifest_rejects_duplicate_target_names(self) -> None:
        raw = {
            "schema": 1,
            "name": "bad",
            "targets": [
                {"name": "same", "address": "0x1000"},
                {"name": "same", "address": "0x2000"},
            ],
        }
        with self.assertRaisesRegex(capture.CaptureError, "Duplicate target name"):
            capture.validate_manifest(raw)

    def test_multi_target_capture_is_ordered_and_bulk_reads_memory(self) -> None:
        client = FakeClient()
        manifest = make_manifest()

        transcript = capture.run_capture(client, manifest)

        self.assertTrue(transcript["complete"])
        self.assertEqual(
            [item["target"] for item in transcript["captures"]],
            ["before", "after"],
        )
        self.assertEqual(
            transcript["captures"][0]["memory"]["packet"]["address"],
            "0x00004020",
        )
        self.assertEqual(
            transcript["captures"][1]["memory"]["receive"]["hex"],
            "00010203",
        )

    def test_arm_and_cleanup_preserve_user_breakpoints(self) -> None:
        client = FakeClient()
        manifest = make_manifest()

        armed = capture.arm_targets(client, manifest)
        removed = capture.cleanup_targets(client, manifest)

        self.assertEqual(armed, ["0x00002000"])
        self.assertEqual(removed, ["0x00002000"])
        self.assertEqual(client.removed, [0x2000])
        self.assertEqual(client.breakpoints[0]["description"], "user breakpoint")

    def test_debug_server_client_spaces_independent_connections(self) -> None:
        first = self.FakeSocket()
        second = self.FakeSocket()
        client = capture.DebugServerClient(minimum_request_interval=0.1)

        with mock.patch.object(capture.socket, "create_connection", side_effect=[first, second]) as connect, \
             mock.patch.object(capture.time, "sleep") as sleep:
            client.request("status")
            client.request("list_breakpoints")

        self.assertEqual(connect.call_count, 2)
        sleep.assert_called_once()
        self.assertGreater(sleep.call_args.args[0], 0.0)
        self.assertIn(b'"cmd":"status"', first.sent)
        self.assertIn(b'"cmd":"list_breakpoints"', second.sent)

    def test_capture_rejects_unsafe_request_spacing_before_connecting(self) -> None:
        manifest = REPO_ROOT / "tools" / "sif-capture.example.json"
        with mock.patch.object(capture.socket, "create_connection") as connect:
            result = capture.main(
                [str(manifest), "--validate-only", "--request-interval-ms", "20"]
            )

        self.assertEqual(result, 1)
        connect.assert_not_called()


if __name__ == "__main__":
    unittest.main()
