from __future__ import annotations

import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pcsx2_boot_exit_writer as writer


class FakeClient:
    def __init__(self) -> None:
        self.memchecks: list[dict[str, object]] = []
        self.breakpoints: list[dict[str, object]] = []
        self.requests: list[tuple[str, dict[str, object]]] = []

    def list_memchecks(self, cpu: str) -> list[dict[str, object]]:
        self._ee(cpu)
        return list(self.memchecks)

    def remove_memcheck(self, cpu: str, start: int, end: int) -> None:
        self._ee(cpu)
        self.memchecks = [
            mc for mc in self.memchecks
            if not (int(str(mc["start"]), 16) == start and int(str(mc["end"]), 16) == end)
        ]

    def list_breakpoints(self, cpu: str) -> list[dict[str, object]]:
        self._ee(cpu)
        return list(self.breakpoints)

    def remove_breakpoint(self, cpu: str, address: int) -> None:
        self._ee(cpu)
        self.breakpoints = [
            bp for bp in self.breakpoints if int(str(bp["address"]), 16) != address
        ]

    def request(self, command: str, *, cpu: str = "ee", **params: object) -> dict[str, object]:
        self._ee(cpu)
        self.requests.append((command, params))
        if command == "set_breakpoint":
            self.breakpoints.append(
                {"address": params["address"], "description": params.get("description", "")}
            )
        return {"ok": True}

    @staticmethod
    def _ee(cpu: str) -> None:
        if cpu != "ee":
            raise AssertionError(cpu)


class MemoryClient:
    def __init__(self) -> None:
        self.memory = bytearray(0x200000)

    def read_memory(self, cpu: str, address: int, length: int) -> str:
        if cpu != "ee":
            raise AssertionError(cpu)
        if address < 0 or address + length > len(self.memory):
            raise writer.OracleError("out of range")
        return bytes(self.memory[address:address + length]).hex()

    def write_u32(self, address: int, value: int) -> None:
        self.memory[address:address + 4] = (value & 0xFFFFFFFF).to_bytes(4, "little")


def make_gpr(**values: int) -> dict[str, object]:
    regs = []
    defaults = {"gp": 0, "v0": 0, "ra": 0}
    defaults.update(values)
    for name, value in defaults.items():
        regs.append(
            {
                "name": name,
                "value": f"000000000000000000000000{value & 0xFFFFFFFF:08x}",
                "display": f"0x{value & 0xFFFFFFFF:08X}.00000000.00000000.00000000",
            }
        )
    return {"GPR": {"count": len(regs), "regs": regs, "size": 128}}


class BootExitWriterTests(unittest.TestCase):
    def test_v4_uses_only_three_executable_breakpoints(self) -> None:
        client = FakeClient()
        writer.arm_trace(client)
        self.assertEqual(client.memchecks, [])
        self.assertEqual(
            {int(str(bp["address"]), 16) for bp in client.breakpoints},
            {writer.CANDIDATE_RETURN_PC, writer.BOOT_LOOP_ESCAPE_PC, writer.DISPATCHER_RETURN_PC},
        )

    def test_source_addresses_distinguish_15f600_from_boot_exit_writer(self) -> None:
        self.assertEqual(writer.BRANCH_STATE_WRITE_PC, 0x22E190)
        self.assertEqual(writer.CANDIDATE_RETURN_PC, 0x22E19C)
        self.assertEqual(writer.BOOT_EXIT_WRITE_PC, 0x22E1A0)
        self.assertEqual(writer.BOOT_EXIT_FLAG, 0x15F5B0)
        self.assertNotEqual(writer.BRANCH_STATE_WRITE_PC, writer.BOOT_EXIT_WRITE_PC)

    def test_gp_relative_writer_attributes_exact_destination(self) -> None:
        regs = make_gpr(gp=0x166C00, v0=1, ra=0x1EBC08)
        evidence = writer.attribute_gp_relative_writer(regs)
        self.assertEqual(evidence["writer_pc"], "0x0022e1a0")
        self.assertEqual(evidence["destination"], "0x0015f5b0")
        self.assertEqual(evidence["value"], "0x00000001")
        self.assertTrue(evidence["gp_matches_expected_boot_abi"])

    def test_gp_relative_writer_fails_closed_on_wrong_gp(self) -> None:
        regs = make_gpr(gp=0x166C04, v0=1, ra=0x1EBC08)
        with self.assertRaisesRegex(writer.OracleError, "destination mismatch"):
            writer.attribute_gp_relative_writer(regs)

    def test_gp_relative_writer_fails_closed_on_zero_value(self) -> None:
        regs = make_gpr(gp=0x166C00, v0=0, ra=0x1EBC08)
        with self.assertRaisesRegex(writer.OracleError, "write zero"):
            writer.attribute_gp_relative_writer(regs)

    def test_callback_state_mirrors_owner_table_and_targets(self) -> None:
        client = MemoryClient()
        client.write_u32(writer.CALLBACK_2195_OWNER, 0x180000)
        client.write_u32(0x180044 + 0 * 4, 0x181000)
        client.write_u32(0x180044 + 2 * 4, 0x181010)
        client.write_u32(0x180044 + 13 * 4, 0x181020)
        client.write_u32(0x181000, writer.CALLBACK_21E7C8)
        client.write_u32(0x181010, 0x002192A8)
        client.write_u32(0x181020, 0x0021E890)

        state = writer.read_callback_state(client)

        self.assertEqual(state["owner_pointer"], "0x00180000")
        self.assertEqual(state["table_pointer"], "0x00180044")
        self.assertTrue(state["table_readable"])
        self.assertEqual(state["non_null_objects"], 3)
        self.assertEqual(state["readable_objects"], 3)
        self.assertEqual(state["non_null_targets"], 3)
        self.assertEqual(state["targets"][0], "0x0021e7c8")
        self.assertEqual(state["targets"][2], "0x002192a8")
        self.assertEqual(state["targets"][13], "0x0021e890")
        self.assertTrue(state["callback21e7c8_present"])
        self.assertEqual(state["callback21e7c8_slot"], 0)

    def test_callback_state_fails_closed_for_unreadable_table(self) -> None:
        client = MemoryClient()
        client.write_u32(writer.CALLBACK_2195_OWNER, 0x1FFFF0)

        state = writer.read_callback_state(client)

        self.assertEqual(state["owner_pointer"], "0x001ffff0")
        self.assertEqual(state["table_pointer"], "0x00000000")
        self.assertFalse(state["table_readable"])
        self.assertEqual(state["non_null_objects"], 0)
        self.assertFalse(state["callback21e7c8_present"])

    def test_cleanup_removes_v3_memcheck_and_old_breakpoints(self) -> None:
        client = FakeClient()
        client.memchecks = [
            {
                "start": "0x0015f5b0",
                "end": "0x0015f5b4",
                "description": "OpenRatchet boot-exit-writer-v3:15F5B0-onchange-log",
            },
            {"start": "0x00100000", "end": "0x00100004", "description": "user watch"},
        ]
        client.breakpoints = [
            {"address": "0x0012da00", "description": "OpenRatchet boot-exit-writer-v3:dispatcher-return"},
            {"address": "0x00200000", "description": "user bp"},
        ]
        writer.remove_owned_artifacts(client)
        self.assertEqual([mc["description"] for mc in client.memchecks], ["user watch"])
        self.assertEqual([bp["description"] for bp in client.breakpoints], ["user bp"])

    def test_generated_source_proves_delay_slot_layout_when_present(self) -> None:
        source = REPO_ROOT / "generated" / "sub_0022E188_0x22e188.cpp"
        if not source.is_file():
            self.skipTest("generated Boot source not present in this checkout")
        text = source.read_text(encoding="utf-8", errors="replace")
        self.assertIn("// 0x22e190:", text)
        self.assertIn("FAST_WRITE32(0x15F600u", text)
        self.assertIn("// 0x22e1a0:", text)
        self.assertIn("sw          $v0, -0x7650($gp) (Delay Slot)", text)

    def test_assert_foreign_debug_artifacts_rejects_user_breakpoints(self) -> None:
        client = FakeClient()
        client.breakpoints = [{"address": "0x00200000", "description": "user bp"}]
        with self.assertRaisesRegex(writer.OracleError, "unrelated debugger"):
            writer.assert_no_foreign_debug_artifacts(client)

    def test_inspect_requires_writer_destination_and_control_flow(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            good = Path(td) / "good.zip"
            with zipfile.ZipFile(good, "w") as archive:
                archive.writestr(
                    "trace.json",
                    json.dumps(
                        {
                            "kind": writer.KIND,
                            "schema": writer.SCHEMA,
                            "result": {
                                "writer_pc": "0x0022e1a0",
                                "writer_destination": "0x0015f5b0",
                                "writer_value": "0x00000001",
                                "writer_gp": "0x00166c00",
                                "writer_seen": True,
                                "boot_loop_escape_seen": True,
                                "dispatcher_return_seen": True,
                                "boot_exit_flag": "0x00000001",
                                "processed_edges": "0x00000000",
                            },
                        }
                    ),
                )
            self.assertEqual(writer.inspect(good), 0)

            bad = Path(td) / "bad.zip"
            with zipfile.ZipFile(bad, "w") as archive:
                archive.writestr(
                    "trace.json",
                    json.dumps(
                        {
                            "kind": writer.KIND,
                            "schema": writer.SCHEMA,
                            "result": {
                                "writer_pc": "0x0022e1a0",
                                "writer_destination": "0x0015f600",
                                "writer_seen": True,
                                "boot_loop_escape_seen": True,
                                "dispatcher_return_seen": True,
                            },
                        }
                    ),
                )
            with self.assertRaisesRegex(writer.OracleError, "destination"):
                writer.inspect(bad)


if __name__ == "__main__":
    unittest.main()
